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


int lw_next_entity_turn(LwState *state) {
    if (state == NULL) return -1;
    if (state->n_in_order <= 0) return -1;

    /* Walk forward up to n_in_order positions, looping if needed. We
     * cap at n_in_order to avoid infinite loops when no one is alive. */
    int start = state->order_index;
    for (int step = 0; step < state->n_in_order; step++) {
        int pos = state->order_index;
        int eid = state->initial_order[pos];

        /* Advance for next call. */
        state->order_index = (pos + 1) % state->n_in_order;
        /* If we wrapped around, that means the round just completed. */
        if (state->order_index == 0 && pos != 0) {
            state->turn++;
            for (int i = 0; i < state->n_entities; i++) {
                if (state->entities[i].alive) {
                    state->entities[i].used_tp = 0;
                    state->entities[i].used_mp = 0;
                }
            }
        }

        if (eid < 0 || eid >= state->n_entities) continue;
        if (!state->entities[eid].alive) continue;
        return eid;
    }

    /* Special case: order_index started at 0 and nothing alive in the
     * full pass. Make the empty-pass count as a round if we fell off
     * the loop without finding an alive entity (matches "everyone
     * dead" -> fight ends, but Python's loop simply terminates). */
    (void)start;
    return -1;
}


int lw_entity_start_turn(LwState *state, int entity_idx) {
    if (state == NULL) return 0;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return 0;
    LwEntity *e = &state->entities[entity_idx];
    if (!e->alive) return 0;

    /* Reset used resources first (Python's Entity.startTurn does this
     * before applying tick effects). */
    e->used_tp = 0;
    e->used_mp = 0;

    return lw_turn_start(state, entity_idx);
}


int lw_entity_end_turn(LwState *state, int entity_idx) {
    return lw_turn_end(state, entity_idx);
}
