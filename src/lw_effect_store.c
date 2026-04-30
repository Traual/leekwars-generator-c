/*
 * lw_effect_store.c -- entity effect-list manipulation.
 *
 * The semantics are designed to mirror Python's Effect / Entity
 * methods (addEffect, removeEffect, reduceEffects, clearPoisons,
 * decrementTurns) by-byte:
 *
 *   - Lists are append-only; removal compacts (preserves relative order
 *     for unaffected entries).
 *   - Removing a buff effect MUST unwind the buff_stats[] deltas it
 *     applied at creation time, otherwise stats slowly drift.
 *   - reduce() floors percent at 0 and uses java_round on each scaled
 *     stat -- this gets the off-by-one rounding right for tests that
 *     replay Python output.
 */

#include "lw_effect_store.h"
#include <math.h>
#include <string.h>


static int java_round(double x) {
    return (int)floor(x + 0.5);
}


void lw_effect_init(LwEffect *e) {
    if (e == NULL) return;
    memset(e, 0, sizeof(*e));
    e->aoe            = 1.0;
    e->critical_power = 1.0;
    e->target_id      = -1;
    e->caster_id      = -1;
    e->attack_id      = -1;
}


int lw_effect_add(LwEntity *target, const LwEffect *src) {
    if (target == NULL || src == NULL) return -1;
    if (target->n_effects >= LW_MAX_EFFECTS) return -1;
    int slot = target->n_effects;
    target->effects[slot] = *src;
    target->n_effects++;
    return slot;
}


void lw_effect_remove(LwEntity *target, int idx) {
    if (target == NULL) return;
    if (idx < 0 || idx >= target->n_effects) return;

    /* Unwind the buff_stats deltas this effect applied. */
    const LwEffect *e = &target->effects[idx];
    for (int s = 0; s < LW_STAT_COUNT; s++) {
        target->buff_stats[s] -= e->stats[s];
    }

    /* Compact: shift the tail left by one. */
    for (int i = idx; i < target->n_effects - 1; i++) {
        target->effects[i] = target->effects[i + 1];
    }
    target->n_effects--;
    /* Zero the trailing slot to keep state hashes deterministic. */
    memset(&target->effects[target->n_effects], 0, sizeof(LwEffect));
}


int lw_effect_remove_by_id(LwEntity *target,
                           int id,
                           int caster_id,
                           int attack_id) {
    if (target == NULL) return 0;
    for (int i = 0; i < target->n_effects; i++) {
        const LwEffect *e = &target->effects[i];
        if (e->id == id &&
            e->caster_id == caster_id &&
            e->attack_id == attack_id) {
            lw_effect_remove(target, i);
            return 1;
        }
    }
    return 0;
}


int lw_effect_clear_poisons(LwEntity *target) {
    if (target == NULL) return 0;
    int removed = 0;
    /* Walk back-to-front so removal indices stay valid. */
    for (int i = target->n_effects - 1; i >= 0; i--) {
        if (target->effects[i].id == LW_EFFECT_POISON) {
            lw_effect_remove(target, i);
            removed++;
        }
    }
    return removed;
}


int lw_effect_decrement_turns(LwEntity *target) {
    if (target == NULL) return 0;
    int removed = 0;
    /* Back-to-front so we can drop expired entries cleanly. */
    for (int i = target->n_effects - 1; i >= 0; i--) {
        LwEffect *e = &target->effects[i];
        if (e->turns > 0) {
            e->turns--;
            if (e->turns == 0) {
                lw_effect_remove(target, i);
                removed++;
            }
        }
    }
    return removed;
}


/* Internal: scale one effect's value/stats by ``factor``, propagating
 * the stat delta back into target.buff_stats[]. Mirrors Effect.reduce
 * in Python (per-stat sign preservation, java_round on absolute value,
 * delta applied to target). */
static void scale_effect_inplace(LwEntity *target, LwEffect *e, double factor) {
    if (factor < 0.0) factor = 0.0;
    e->value = java_round((double)e->value * factor);

    for (int s = 0; s < LW_STAT_COUNT; s++) {
        int cur = e->stats[s];
        if (cur == 0) continue;
        int sign = (cur > 0) ? 1 : -1;
        int abs_cur = (cur > 0) ? cur : -cur;
        int new_val = java_round((double)abs_cur * factor) * sign;
        int delta = new_val - cur;
        if (delta != 0) {
            e->stats[s] += delta;
            target->buff_stats[s] += delta;
        }
    }
}


void lw_effect_reduce(LwEntity *target, double percent) {
    if (target == NULL) return;
    double factor = 1.0 - percent;
    for (int i = 0; i < target->n_effects; i++) {
        LwEffect *e = &target->effects[i];
        if (e->modifiers & LW_MODIFIER_IRREDUCTIBLE) continue;
        scale_effect_inplace(target, e, factor);
    }
}


void lw_effect_reduce_total(LwEntity *target, double percent) {
    if (target == NULL) return;
    double factor = 1.0 - percent;
    for (int i = 0; i < target->n_effects; i++) {
        scale_effect_inplace(target, &target->effects[i], factor);
    }
}
