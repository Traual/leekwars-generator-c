/*
 * lw_damage.c -- damage application matching Python byte-for-byte.
 *
 * Reference: leekwars/effect/effect_damage.py
 *
 * Steps (mirroring the reference exactly):
 *   1. d = (value1 + jet * value2)
 *          * (1 + max(0, caster.strength) / 100.0)
 *          * aoe * critical_power * target_count
 *          * (1 + caster.power / 100.0)
 *   2. shields:
 *        d -= d * (target.relative_shield / 100.0)
 *           + target.absolute_shield
 *   3. d = max(0, d)
 *   4. INVINCIBLE state -> d = 0
 *   5. dealt = java_round(d), capped at target.life
 *   6. target.life -= dealt
 *
 * java_round: Python's java_round implements Java Math.round() which
 * is "half-up" rounding away from -infinity for positive values
 * (i.e. floor(d + 0.5)). We replicate that exactly.
 */

#include "lw_damage.h"
#include "lw_attack_apply.h"  /* for lw_event_on_* hooks */
#include <math.h>


/* Java Math.round = floor(x + 0.5). Differs from C's lround for
 * negative half-values and ties; for damage (always >= 0) the
 * implementations agree. */
static int java_round(double x) {
    /* Match Python java_round: floor(x + 0.5). */
    return (int)floor(x + 0.5);
}


static int stat(const LwEntity *e, int idx) {
    return e->base_stats[idx] + e->buff_stats[idx];
}


/* Internal: Python-equivalent removeLife.
 * pv reduces hp (capped at remaining); erosion reduces total_hp
 * (floor 1). Marks dead if hp hits 0. Returns the actual pv applied. */
static int lw_remove_life(LwState *state, LwEntity *e,
                          int pv, int erosion) {
    if (e->hp <= 0) return 0;
    if (pv > e->hp) pv = e->hp;
    if (pv < 0) pv = 0;
    e->hp -= pv;
    e->total_hp -= erosion;
    if (e->total_hp < 1) e->total_hp = 1;
    if (e->hp <= 0) {
        e->hp = 0;
        e->alive = 0;
        if (e->cell_id >= 0) {
            state->map.entity_at_cell[e->cell_id] = -1;
            e->cell_id = -1;
        }
    }
    return pv;
}


int lw_apply_damage_v2(LwState *state,
                       int caster_idx,
                       int target_idx,
                       double value1,
                       double value2,
                       double jet,
                       double aoe,
                       double critical_power,
                       int    target_count,
                       double erosion_rate) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;

    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;

    /* Step 1: base damage. */
    int strength = stat(caster, LW_STAT_STRENGTH);
    int power    = stat(caster, LW_STAT_POWER);
    if (strength < 0) strength = 0;  /* Python uses max(0, strength) */

    double d = (value1 + jet * value2)
             * (1.0 + (double)strength / 100.0)
             * aoe
             * critical_power
             * (double)target_count
             * (1.0 + (double)power / 100.0);

    /* Step 2 (Python order: BEFORE shields): compute return_damage. */
    int return_damage = 0;
    if (target_idx != caster_idx) {
        int dr = stat(target, LW_STAT_DAMAGE_RETURN);
        return_damage = java_round(d * (double)dr / 100.0);
    }

    /* Step 3: shields. */
    int rel = stat(target, LW_STAT_RELATIVE_SHIELD);
    int abs_sh = stat(target, LW_STAT_ABSOLUTE_SHIELD);
    d -= d * ((double)rel / 100.0) + (double)abs_sh;

    /* Step 4: floor at 0. */
    if (d < 0.0) d = 0.0;

    /* Step 5: INVINCIBLE shortcut. */
    if (target->state_flags & LW_STATE_INVINCIBLE) {
        d = 0.0;
    }

    /* Step 6: round + cap to remaining HP. */
    int dealt = java_round(d);
    if (dealt < 0) dealt = 0;
    if (dealt > target->hp) dealt = target->hp;

    /* Step 7: life steal from rounded value AFTER shields. */
    int life_steal = 0;
    if (target_idx != caster_idx) {
        int wisdom = stat(caster, LW_STAT_WISDOM);
        life_steal = java_round((double)dealt * (double)wisdom / 1000.0);
    }

    /* Step 8: target removeLife with erosion. */
    int erosion = (erosion_rate > 0.0)
                ? java_round((double)dealt * erosion_rate) : 0;
    int target_was_alive = target->alive;
    lw_remove_life(state, target, dealt, erosion);

    /* Action stream: log the damage event. Matches Python's
     * state.log(ActionDamage(...)) emitted right before removeLife. */
    if (dealt > 0 || erosion > 0) {
        lw_action_emit(state, LW_ACT_DAMAGE, caster_idx, target_idx,
                        dealt, erosion, 0);
    }

    /* Passive event hooks (mirror Python's order: removeLife ->
     * onDirectDamage -> onNovaDamage). */
    if (dealt > 0)   lw_event_on_direct_damage(state, target_idx, dealt);
    if (erosion > 0) lw_event_on_nova_damage(state, target_idx, erosion);

    /* If the target died on this hit, fire on_kill (caster) +
     * on_ally_killed (allies of the dead, excluding self).
     *
     * Python does NOT emit ActionKill for damage-induced deaths --
     * only EffectKill (Effect.TYPE_KILL, the one-shot kill effect)
     * emits ActionKill. Damage-deaths are signalled implicitly by the
     * LOST_LIFE event reducing target HP to 0. We mirror that here:
     * fire the passive hooks but DO NOT emit LW_ACT_KILL. */
    if (target_was_alive && !target->alive) {
        lw_event_on_kill(state, caster_idx);
        lw_event_on_ally_killed(state, target_idx);
    }

    /* Step 9: life steal -- heals caster up to missing HP, blocked by
     * UNHEALABLE on caster, no-op if caster died. */
    if (life_steal > 0 &&
        caster->alive &&
        !(caster->state_flags & LW_STATE_UNHEALABLE) &&
        caster->hp < caster->total_hp) {
        int missing = caster->total_hp - caster->hp;
        if (life_steal > missing) life_steal = missing;
        if (life_steal > 0) {
            caster->hp += life_steal;
        }
    }

    /* Step 10: return damage. Blocked by caster INVINCIBLE. Erosion
     * derived from the (possibly capped) returnDamage at the same rate. */
    if (return_damage > 0 && !(caster->state_flags & LW_STATE_INVINCIBLE)) {
        if (return_damage > caster->hp) return_damage = caster->hp;
        if (return_damage > 0) {
            int return_erosion = (erosion_rate > 0.0)
                               ? java_round((double)return_damage * erosion_rate) : 0;
            int caster_was_alive = caster->alive;
            lw_remove_life(state, caster, return_damage, return_erosion);
            /* Caster takes its own onNovaDamage from the return-damage erosion.
             * Python doesn't fire onDirectDamage here -- the return-damage path
             * only does removeLife + onNovaDamage. */
            if (return_erosion > 0)
                lw_event_on_nova_damage(state, caster_idx, return_erosion);
            if (caster_was_alive && !caster->alive) {
                /* Reflected damage killed the caster. Python's flow has
                 * the killer be the original target in this case. */
                lw_event_on_kill(state, target_idx);
                lw_event_on_ally_killed(state, caster_idx);
            }
        }
    }

    return dealt;
}


int lw_apply_damage(LwState *state,
                    int caster_idx,
                    int target_idx,
                    double value1,
                    double value2,
                    double jet,
                    double aoe,
                    double critical_power,
                    int    target_count) {
    /* Backwards-compat wrapper: no erosion. */
    return lw_apply_damage_v2(state, caster_idx, target_idx,
                              value1, value2, jet, aoe, critical_power,
                              target_count, 0.0);
}


int lw_apply_heal(LwState *state,
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
    LwEntity *caster = &state->entities[caster_idx];
    LwEntity *target = &state->entities[target_idx];
    if (!target->alive) return 0;
    if (target->state_flags & LW_STATE_UNHEALABLE) return 0;

    int wisdom = stat(caster, LW_STAT_WISDOM);
    double v = (value1 + jet * value2)
             * (1.0 + (double)wisdom / 100.0)
             * aoe * critical_power * (double)target_count;
    int amount = java_round(v);
    if (amount < 0) amount = 0;

    int missing = target->total_hp - target->hp;
    if (amount > missing) amount = missing;

    target->hp += amount;
    if (amount > 0) {
        lw_action_emit(state, LW_ACT_HEAL, caster_idx, target_idx,
                        amount, 0, 0);
    }
    return amount;
}


int lw_apply_absolute_shield(LwState *state,
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

    int resistance = stat(caster, LW_STAT_RESISTANCE);
    double v = (value1 + jet * value2)
             * (1.0 + (double)resistance / 100.0)
             * aoe * critical_power;
    int amount = java_round(v);
    if (amount > 0) {
        target->buff_stats[LW_STAT_ABSOLUTE_SHIELD] += amount;
    }
    return amount > 0 ? amount : 0;
}


int lw_apply_relative_shield(LwState *state,
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

    int resistance = stat(caster, LW_STAT_RESISTANCE);
    double v = (value1 + jet * value2)
             * (1.0 + (double)resistance / 100.0)
             * aoe * critical_power;
    int amount = java_round(v);
    if (amount > 0) {
        target->buff_stats[LW_STAT_RELATIVE_SHIELD] += amount;
    }
    return amount > 0 ? amount : 0;
}


int lw_apply_erosion(LwState *state, int target_idx,
                     int value, double rate) {
    /* Python: erosion = round(value * rate); total_hp -= erosion;
     * floor total_hp at 1. */
    if (state == NULL) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    if (value <= 0 || rate <= 0.0) return 0;

    LwEntity *target = &state->entities[target_idx];
    int erosion = java_round((double)value * rate);
    if (erosion <= 0) return 0;

    target->total_hp -= erosion;
    if (target->total_hp < 1) target->total_hp = 1;
    return erosion;
}


double lw_erosion_rate(int effect_type, int is_critical) {
    /* Effect.EROSION_DAMAGE (0.05) for direct/aftereffect damage,
     * EROSION_POISON (0.10) for poison. Crit adds EROSION_CRITICAL_BONUS
     * (0.10) on top. */
    double base = 0.05;
    if (effect_type == 13 /* LW_EFFECT_POISON */) base = 0.10;
    if (is_critical) base += 0.10;
    return base;
}
