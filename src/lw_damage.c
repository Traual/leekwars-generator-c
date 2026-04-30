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


int lw_apply_damage(LwState *state,
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

    /* Step 3: shields. Python applies BOTH relative pct AND absolute
     * subtraction in a single expression -- so the order matches:
     *   d -= d * relshield/100 + abs_shield
     */
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

    /* Step 7 (Python order): compute life steal from rounded ``value``
     * AFTER shields, BEFORE removing HP. */
    int life_steal = 0;
    if (target_idx != caster_idx) {
        int wisdom = stat(caster, LW_STAT_WISDOM);
        life_steal = java_round((double)dealt * (double)wisdom / 1000.0);
    }

    /* Step 8: subtract target HP, mark dead if needed. */
    target->hp -= dealt;
    if (target->hp <= 0) {
        target->hp = 0;
        target->alive = 0;
        if (target->cell_id >= 0) {
            state->map.entity_at_cell[target->cell_id] = -1;
            target->cell_id = -1;
        }
    }

    /* Step 9: life steal — heals caster up to missing HP, blocked by
     * UNHEALABLE on caster, no-op if caster died (e.g. self-target with
     * crazy modifiers). */
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

    /* Step 10: return damage to caster from target's DAMAGE_RETURN
     * buff. Blocked by INVINCIBLE on caster, capped at caster HP. */
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
