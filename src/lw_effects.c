/*
 * lw_effects.c -- buff / poison / shackle / vitality / debuff
 * effect ports.
 *
 * Most apply paths come from leekwars/effect/*.py one-to-one. Each
 * function is an in-place mutator on LwState; returns the "headline"
 * value (damage dealt, heal granted, debuff magnitude). Erosion is
 * NOT yet handled here -- the existing damage paths will be
 * retrofitted in a focused refactor pass once the catalog is wide
 * enough to motivate a unified DamageResult struct.
 */

#include "lw_effects.h"
#include "lw_effect_store.h"
#include <math.h>


static int java_round(double x) {
    return (int)floor(x + 0.5);
}


static int stat(const LwEntity *e, int idx) {
    return e->base_stats[idx] + e->buff_stats[idx];
}


int lw_apply_buff_stat(LwState *state,
                       int caster_idx,
                       int target_idx,
                       int stat_index,
                       int scale_stat,
                       double value1,
                       double value2,
                       double jet,
                       double aoe,
                       double critical_power) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    if (stat_index  < 0 || stat_index  >= LW_STAT_COUNT) return 0;
    if (scale_stat  < 0 || scale_stat  >= LW_STAT_COUNT) return 0;
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];

    int scale = stat(caster, scale_stat);
    double v = (value1 + value2 * jet)
             * (1.0 + (double)scale / 100.0)
             * aoe * critical_power;
    int amount = java_round(v);
    if (amount > 0) {
        target->buff_stats[stat_index] += amount;
    }
    return amount > 0 ? amount : 0;
}


int lw_apply_aftereffect(LwState *state,
                          int caster_idx,
                          int target_idx,
                          double value1,
                          double value2,
                          double jet,
                          double aoe,
                          double critical_power) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int science = stat(caster, LW_STAT_SCIENCE);
    double v = (value1 + value2 * jet)
             * (1.0 + (double)science / 100.0)
             * aoe * critical_power;
    int amount = java_round(v);
    if (amount < 0) amount = 0;

    if (target->state_flags & LW_STATE_INVINCIBLE) amount = 0;
    if (amount > target->hp) amount = target->hp;

    if (amount > 0) {
        target->hp -= amount;
        if (target->hp <= 0) {
            target->hp = 0;
            target->alive = 0;
            if (target->cell_id >= 0) {
                state->map.entity_at_cell[target->cell_id] = -1;
                target->cell_id = -1;
            }
        }
    }
    return amount;
}


int lw_compute_poison_damage(const LwState *state,
                             int caster_idx,
                             int target_idx,
                             double value1,
                             double value2,
                             double jet,
                             double aoe,
                             double critical_power) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    const LwEntity *caster = &state->entities[caster_idx];

    int magic = stat(caster, LW_STAT_MAGIC);
    int power = stat(caster, LW_STAT_POWER);
    if (magic < 0) magic = 0;
    /* Python: max(0, getMagic()), getPower() unclipped. */

    double v = (value1 + jet * value2)
             * (1.0 + (double)magic / 100.0)
             * aoe * critical_power
             * (1.0 + (double)power / 100.0);
    int per_turn = java_round(v);
    if (per_turn < 0) per_turn = 0;
    return per_turn;
}


int lw_tick_poison(LwState *state, int target_idx, int per_turn_damage) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int damages = per_turn_damage;
    if (target->hp < damages) damages = target->hp;
    if (target->state_flags & LW_STATE_INVINCIBLE) damages = 0;

    if (damages > 0) {
        target->hp -= damages;
        if (target->hp <= 0) {
            target->hp = 0;
            target->alive = 0;
            if (target->cell_id >= 0) {
                state->map.entity_at_cell[target->cell_id] = -1;
                target->cell_id = -1;
            }
        }
    }
    return damages;
}


int lw_tick_aftereffect(LwState *state, int target_idx, int per_turn_damage) {
    /* EffectAftereffect.applyStartTurn -- same shape as poison tick:
     * cap to remaining HP, INVINCIBLE -> 0, subtract HP, kill if 0. */
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int amount = per_turn_damage;
    if (target->state_flags & LW_STATE_INVINCIBLE) amount = 0;
    if (amount > target->hp) amount = target->hp;
    if (amount < 0) amount = 0;

    if (amount > 0) {
        target->hp -= amount;
        if (target->hp <= 0) {
            target->hp = 0;
            target->alive = 0;
            if (target->cell_id >= 0) {
                state->map.entity_at_cell[target->cell_id] = -1;
                target->cell_id = -1;
            }
        }
    }
    return amount;
}


int lw_tick_heal(LwState *state, int target_idx, int per_turn_heal) {
    /* EffectHeal.applyStartTurn -- heal capped at missing HP,
     * UNHEALABLE -> no-op (returns 0). */
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;
    if (target->state_flags & LW_STATE_UNHEALABLE) return 0;

    int amount = per_turn_heal;
    if (amount < 0) amount = 0;
    int missing = target->total_hp - target->hp;
    if (amount > missing) amount = missing;

    target->hp += amount;
    return amount;
}


int lw_apply_shackle(LwState *state,
                     int caster_idx,
                     int target_idx,
                     int stat_index,
                     double value1,
                     double value2,
                     double jet,
                     double aoe,
                     double critical_power) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    if (stat_index  < 0 || stat_index  >= LW_STAT_COUNT) return 0;
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];

    int magic = stat(caster, LW_STAT_MAGIC);
    if (magic < 0) magic = 0;  /* Python: max(0, magic) */

    double v = (value1 + value2 * jet)
             * (1.0 + (double)magic / 100.0)
             * aoe * critical_power;
    int amount = java_round(v);
    if (amount > 0) {
        target->buff_stats[stat_index] -= amount;
    }
    return amount > 0 ? amount : 0;
}


int lw_apply_vitality(LwState *state,
                      int caster_idx,
                      int target_idx,
                      double value1,
                      double value2,
                      double jet,
                      double aoe,
                      double critical_power) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int wisdom = stat(caster, LW_STAT_WISDOM);
    double v = (value1 + jet * value2)
             * (1.0 + (double)wisdom / 100.0)
             * aoe * critical_power;
    int amount = java_round(v);
    if (amount < 0) amount = 0;

    /* Python: addTotalLife then addLife (both by same amount) -- so the
     * entity's max HP grows AND current HP grows. */
    target->total_hp += amount;
    target->hp       += amount;
    return amount;
}


int lw_apply_nova_vitality(LwState *state,
                           int caster_idx,
                           int target_idx,
                           double value1,
                           double value2,
                           double jet,
                           double aoe,
                           double critical_power) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int science = stat(caster, LW_STAT_SCIENCE);
    double v = (value1 + jet * value2)
             * (1.0 + (double)science / 100.0)
             * aoe * critical_power;
    int amount = java_round(v);
    if (amount < 0) amount = 0;

    /* Python: addTotalLife only (no addLife) -- max HP grows, current
     * HP unchanged. */
    target->total_hp += amount;
    return amount;
}


int lw_apply_raw_heal(LwState *state,
                      int caster_idx,
                      int target_idx,
                      double value1,
                      double value2,
                      double jet,
                      double aoe,
                      double critical_power,
                      int    target_count) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;
    if (target->state_flags & LW_STATE_UNHEALABLE) return 0;

    /* No wisdom scaling here -- pure base*aoe*crit*tc. */
    double v = (value1 + jet * value2)
             * aoe * critical_power * (double)target_count;
    int amount = java_round(v);
    if (amount < 0) amount = 0;

    int missing = target->total_hp - target->hp;
    if (amount > missing) amount = missing;
    target->hp += amount;
    return amount;
}


int lw_apply_steal_life(LwState *state,
                        int target_idx,
                        int previous_value) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;
    if (target->state_flags & LW_STATE_UNHEALABLE) return 0;

    int amount = previous_value;
    if (amount <= 0) return 0;

    int missing = target->total_hp - target->hp;
    if (amount > missing) amount = missing;
    target->hp += amount;
    return amount;
}


int lw_apply_nova_damage(LwState *state,
                         int caster_idx,
                         int target_idx,
                         double value1,
                         double value2,
                         double jet,
                         double aoe,
                         double critical_power) {
    /* Reference: leekwars/effect/effect_nova_damage.py
     * d = (v1+jet*v2) * (1 + max(0,science)/100) * aoe * crit
     *     * (1 + power/100)
     * INVINCIBLE -> d=0. Capped at total_hp - hp. Reduces total_hp
     * (via removeLife with pv=0, erosion=value); current HP unchanged. */
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int science = stat(caster, LW_STAT_SCIENCE);
    int power   = stat(caster, LW_STAT_POWER);
    if (science < 0) science = 0;

    double d = (value1 + jet * value2)
             * (1.0 + (double)science / 100.0)
             * aoe * critical_power
             * (1.0 + (double)power / 100.0);

    if (target->state_flags & LW_STATE_INVINCIBLE) d = 0.0;

    int amount = java_round(d);
    if (amount < 0) amount = 0;

    int gap = target->total_hp - target->hp;
    if (amount > gap) amount = gap;

    if (amount > 0) {
        target->total_hp -= amount;
        if (target->total_hp < 1) target->total_hp = 1;
    }
    return amount;
}


int lw_apply_life_damage(LwState *state,
                         int caster_idx,
                         int target_idx,
                         double value1,
                         double value2,
                         double jet,
                         double aoe,
                         double critical_power) {
    /* Reference: leekwars/effect/effect_life_damage.py */
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int caster_life = caster->hp;  /* getLife() in Python */
    int power = stat(caster, LW_STAT_POWER);

    double d = ((value1 + jet * value2) / 100.0)
             * (double)caster_life
             * aoe * critical_power
             * (1.0 + (double)power / 100.0);

    if (target->state_flags & LW_STATE_INVINCIBLE) d = 0.0;

    /* Reflect (only if target != caster). Computed from d BEFORE
     * shields, matching Python's order. */
    int return_damage = 0;
    if (target_idx != caster_idx) {
        int dr = stat(target, LW_STAT_DAMAGE_RETURN);
        return_damage = java_round(d * (double)dr / 100.0);
    }

    /* Shields: same shape as regular damage. */
    int rel_shield = stat(target, LW_STAT_RELATIVE_SHIELD);
    int abs_shield = stat(target, LW_STAT_ABSOLUTE_SHIELD);
    d -= d * ((double)rel_shield / 100.0) + (double)abs_shield;
    if (d < 0.0) d = 0.0;

    int dealt = java_round(d);
    if (dealt < 0) dealt = 0;
    if (dealt > target->hp) dealt = target->hp;

    if (dealt > 0) {
        target->hp -= dealt;
        if (target->hp <= 0) {
            target->hp = 0;
            target->alive = 0;
            if (target->cell_id >= 0) {
                state->map.entity_at_cell[target->cell_id] = -1;
                target->cell_id = -1;
            }
        }
    }

    /* Apply reflect to caster -- skip if caster is now invincible. */
    if (return_damage > 0 && !(caster->state_flags & LW_STATE_INVINCIBLE)) {
        if (return_damage > caster->hp) return_damage = caster->hp;
        if (return_damage > 0) {
            caster->hp -= return_damage;
            if (caster->hp <= 0) {
                caster->hp = 0;
                caster->alive = 0;
                if (caster->cell_id >= 0) {
                    state->map.entity_at_cell[caster->cell_id] = -1;
                    caster->cell_id = -1;
                }
            }
        }
    }
    return dealt;
}


int lw_apply_debuff(LwState *state,
                    int caster_idx,
                    int target_idx,
                    double value1,
                    double value2,
                    double jet,
                    double aoe,
                    double critical_power,
                    int    target_count) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    (void)caster_idx;  /* Python uses caster only for stats logging, not formula. */
    LwEntity *target = &state->entities[target_idx];

    /* Python: int(...) -> truncate toward zero, NOT java_round. */
    double v = (value1 + jet * value2) * aoe * critical_power
             * (double)target_count;
    int amount = (int)v;  /* int() = truncate */
    if (amount < 0) amount = 0;

    if (amount > 0) {
        lw_effect_reduce(target, (double)amount / 100.0);
    }
    return amount;
}


int lw_apply_total_debuff(LwState *state,
                          int caster_idx,
                          int target_idx,
                          double value1,
                          double value2,
                          double jet,
                          double aoe,
                          double critical_power,
                          int    target_count) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    (void)caster_idx;
    LwEntity *target = &state->entities[target_idx];

    double v = (value1 + jet * value2) * aoe * critical_power
             * (double)target_count;
    int amount = (int)v;
    if (amount < 0) amount = 0;

    if (amount > 0) {
        lw_effect_reduce_total(target, (double)amount / 100.0);
    }
    return amount;
}


int lw_apply_antidote(LwState *state, int target_idx) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    return lw_effect_clear_poisons(target);
}


int lw_apply_raw_buff_stat(LwState *state,
                           int target_idx,
                           int stat_index,
                           double value1,
                           double value2,
                           double jet,
                           double aoe,
                           double critical_power) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    if (stat_index  < 0 || stat_index  >= LW_STAT_COUNT) return 0;
    LwEntity *target = &state->entities[target_idx];

    double v = (value1 + jet * value2) * aoe * critical_power;
    int amount = java_round(v);
    if (amount > 0) {
        target->buff_stats[stat_index] += amount;
    }
    return amount > 0 ? amount : 0;
}


int lw_apply_vulnerability(LwState *state,
                           int target_idx,
                           double value1,
                           double value2,
                           double jet,
                           double aoe,
                           double critical_power) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];

    double v = (value1 + value2 * jet) * aoe * critical_power;
    int amount = java_round(v);
    if (amount > 0) {
        target->buff_stats[LW_STAT_RELATIVE_SHIELD] -= amount;
    }
    return amount > 0 ? amount : 0;
}


int lw_apply_absolute_vulnerability(LwState *state,
                                    int target_idx,
                                    double value1,
                                    double value2,
                                    double jet,
                                    double aoe,
                                    double critical_power) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];

    double v = (value1 + value2 * jet) * aoe * critical_power;
    int amount = java_round(v);
    if (amount > 0) {
        target->buff_stats[LW_STAT_ABSOLUTE_SHIELD] -= amount;
    }
    return amount > 0 ? amount : 0;
}


int lw_apply_kill(LwState *state, int caster_idx, int target_idx) {
    /* EffectKill: hp -> 0 directly. No shields, no INVINCIBLE check
     * (matches Python's commented-out branch). Returns life lost. */
    (void)caster_idx;
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    int lost = target->hp;
    target->hp = 0;
    target->alive = 0;
    if (target->cell_id >= 0) {
        state->map.entity_at_cell[target->cell_id] = -1;
        target->cell_id = -1;
    }
    return lost;
}


int lw_apply_add_state(LwState *state, int target_idx, uint32_t state_flag) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];
    target->state_flags |= state_flag;
    return 1;
}


int lw_apply_steal_absolute_shield(LwState *state,
                                   int target_idx,
                                   int previous_value) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    if (previous_value <= 0) return 0;
    LwEntity *target = &state->entities[target_idx];
    target->buff_stats[LW_STAT_ABSOLUTE_SHIELD] += previous_value;
    return previous_value;
}


int lw_apply_remove_shackles(LwState *state, int target_idx) {
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[target_idx];

    int removed = 0;
    /* Walk back-to-front so removal indices stay valid. The shackle
     * effect ids are consecutive in Python's enum but split across the
     * range -- list them explicitly for clarity. */
    for (int i = target->n_effects - 1; i >= 0; i--) {
        int id = target->effects[i].id;
        if (id == LW_EFFECT_SHACKLE_MP ||
            id == LW_EFFECT_SHACKLE_TP ||
            id == LW_EFFECT_SHACKLE_STRENGTH ||
            id == LW_EFFECT_SHACKLE_MAGIC ||
            id == LW_EFFECT_SHACKLE_AGILITY ||
            id == LW_EFFECT_SHACKLE_WISDOM) {
            lw_effect_remove(target, i);
            removed++;
        }
    }
    return removed;
}


int lw_apply_resurrect(LwState *state,
                       int target_idx,
                       int dest_cell,
                       int full_life,
                       int critical) {
    if (state == NULL) return -1;
    if (target_idx < 0 || target_idx >= state->n_entities) return -1;
    LwEntity *t = &state->entities[target_idx];
    if (t->alive) return 0;
    if (dest_cell < 0 || dest_cell >= LW_MAX_CELLS) return -1;
    if (state->map.topo != NULL && dest_cell >= state->map.topo->n_cells) return -1;
    if (state->map.entity_at_cell[dest_cell] >= 0) return -1;

    double factor = critical ? 1.3 : 1.0;
    if (full_life) {
        t->hp = t->total_hp;
    } else {
        int new_total = java_round((double)t->total_hp * 0.5 * factor);
        if (new_total < 10) new_total = 10;
        t->total_hp = new_total;
        t->hp = new_total / 2;
    }
    t->alive = 1;
    t->state_flags |= LW_STATE_RESURRECTED;
    t->cell_id = dest_cell;
    state->map.entity_at_cell[dest_cell] = target_idx;
    return 1;
}


int lw_apply_multiply_stats(LwState *state,
                            int caster_idx,
                            int target_idx,
                            double value1) {
    /* EffectMultiplyStats.apply: multiplies all base stats by factor.
     * Factor stored as int(value1). factor <= 1 -> no-op. */
    (void)caster_idx;
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;

    int factor = (int)value1;
    if (factor <= 1) return 0;

    LwEntity *target = &state->entities[target_idx];

    /* Multiply non-life stats. */
    int slots[] = {
        LW_STAT_STRENGTH, LW_STAT_AGILITY, LW_STAT_RESISTANCE,
        LW_STAT_WISDOM,   LW_STAT_SCIENCE, LW_STAT_MAGIC,
        LW_STAT_FREQUENCY, LW_STAT_TP,     LW_STAT_MP
    };
    int n_slots = (int)(sizeof(slots) / sizeof(slots[0]));
    for (int i = 0; i < n_slots; i++) {
        int s = slots[i];
        int base = target->base_stats[s];
        int buff = base * (factor - 1);
        if (buff > 0) {
            target->buff_stats[s] += buff;
        }
    }

    /* Life: if no previous boost, full multiply; if there is one, just
     * add 1x base. We approximate "previous boost" check by comparing
     * total_hp against base_life (matches Python). */
    int base_life = target->base_stats[LW_STAT_LIFE];
    int life_delta;
    if (target->total_hp <= base_life) {
        life_delta = base_life * (factor - 1);
    } else {
        life_delta = base_life;
    }

    /* Preserve hp/total_hp ratio. */
    double ratio = (target->total_hp > 0)
                 ? ((double)target->hp / (double)target->total_hp)
                 : 1.0;
    target->total_hp += life_delta;

    int new_hp = (int)floor((double)target->total_hp * ratio + 0.5);  /* java_round */
    int heal = new_hp - target->hp;
    if (heal > 0) {
        target->hp += heal;
        if (target->hp > target->total_hp) target->hp = target->total_hp;
    }

    return factor;
}
