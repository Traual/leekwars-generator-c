/*
 * lw_turn.c -- start-of-turn / end-of-turn effect tick driver.
 *
 * For every effect type that implements applyStartTurn in Python,
 * the C engine has a matching tick function (lw_tick_poison /
 * lw_tick_aftereffect / lw_tick_heal). This file dispatches based
 * on effect.id and accumulates damage/heal totals.
 */

#include "lw_turn.h"
#include "lw_effects.h"
#include "lw_effect_store.h"


int lw_turn_start(LwState *state, int entity_idx) {
    if (state == NULL) return 0;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return 0;
    LwEntity *target = &state->entities[entity_idx];
    /* If the entity is already dead at the start of its turn we still
     * walk effects -- a dying entity's poison ticks have already been
     * filtered inside the tick functions (lw_tick_*) by the alive
     * check, so this is a no-op in practice. */
    int net_damage = 0;
    int n = target->n_effects;
    for (int i = 0; i < n; i++) {
        const LwEffect *e = &target->effects[i];
        switch (e->id) {
            case LW_EFFECT_POISON:
                net_damage += lw_tick_poison(state, entity_idx, e->value);
                break;
            case LW_EFFECT_AFTEREFFECT:
                net_damage += lw_tick_aftereffect(state, entity_idx, e->value);
                break;
            case LW_EFFECT_HEAL:
                /* Heal subtracts from "damage" totals -- track as
                 * negative so callers can sum across entities. */
                net_damage -= lw_tick_heal(state, entity_idx, e->value);
                break;
            default:
                break;
        }
        /* If a tick killed the entity, the rest of the effects still
         * iterate (matches Python). */
    }
    return net_damage;
}


int lw_turn_end(LwState *state, int entity_idx) {
    if (state == NULL) return 0;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return 0;
    return lw_effect_decrement_turns(&state->entities[entity_idx]);
}


int lw_turn_end_all(LwState *state) {
    if (state == NULL) return 0;
    int total = 0;
    for (int i = 0; i < state->n_entities; i++) {
        if (state->entities[i].alive) {
            total += lw_turn_end(state, i);
        }
    }
    return total;
}
