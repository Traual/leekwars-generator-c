/*
 * lw_effects.c -- buff / poison effect ports.
 */

#include "lw_effects.h"
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
