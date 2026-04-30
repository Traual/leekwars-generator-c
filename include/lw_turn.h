/*
 * lw_turn.h -- start-of-turn effect tick + end-of-turn cleanup.
 *
 * Mirrors how the Python engine ticks effects:
 *   - At an entity's start-of-turn, its applyStartTurn callbacks fire.
 *     Currently three effect types implement it:
 *       POISON       -> deals stored value as damage
 *       AFTEREFFECT  -> deals stored value as damage
 *       HEAL         -> heals stored value
 *   - At end-of-turn (per-entity), every effect's turns counter
 *     decrements; effects whose counter hits 0 are removed and their
 *     buff_stats deltas are unwound.
 *
 * This module provides the per-entity hooks. The fight-loop wrapper
 * (Fight.startNextEntityTurn / endTurn) will call these in the right
 * order once the turn driver lands.
 */
#ifndef LW_TURN_H
#define LW_TURN_H

#include "lw_state.h"

/*
 * Run applyStartTurn on every active effect for ``entity_idx``.
 *
 * Order: walks effects[] front-to-back so the action stream sees the
 * same order Python produces. Each tick may kill the target (poison /
 * aftereffect on low HP); we keep ticking remaining effects on the
 * dead entity for parity, since Python doesn't short-circuit either
 * (the per-effect dispatch checks alive() inside its tick).
 *
 * Returns total HP dealt by ticks (sum of poison + aftereffect; heals
 * subtracted out as negatives).
 */
int lw_turn_start(LwState *state, int entity_idx);

/*
 * Decrement turns counters for ``entity_idx``'s effects; remove any
 * that hit 0 (unwinding buff_stats[] in the process). Mirrors the
 * Entity.endTurn path that wraps lw_effect_decrement_turns.
 *
 * Returns the number of effects that expired this call.
 */
int lw_turn_end(LwState *state, int entity_idx);

/*
 * End-of-turn for every ALIVE entity. Wraps lw_turn_end in a loop.
 * Returns total expirations across all entities.
 */
int lw_turn_end_all(LwState *state);

/* ---------------- round driver ------------------------------- */

/*
 * Activate the next entity in initial_order. Returns the entity index
 * (NOT the order position) that's now active, or -1 if no alive
 * entity is left in the round.
 *
 * Skips dead entities. When the order rolls past the last position
 * (i.e. a full round just completed), increments state->turn and
 * resets used_tp/used_mp on every alive entity, mirroring how
 * Fight.startNextEntityTurn behaves between rounds.
 */
int lw_next_entity_turn(LwState *state);

/*
 * Hook to call at the start of an entity's turn:
 *   - resets used_tp / used_mp to 0
 *   - runs lw_turn_start (poison / aftereffect / heal ticks)
 * The reset happens BEFORE the tick so a heal on a hot ally
 * actually heals (matches Python's order).
 *
 * Returns the net damage dealt by the start-of-turn ticks (positive
 * = entity took damage, negative = entity gained HP).
 */
int lw_entity_start_turn(LwState *state, int entity_idx);

/*
 * Hook to call at the end of an entity's turn:
 *   - runs lw_turn_end (decrement effect counters)
 *
 * Returns the number of effects that expired this call.
 */
int lw_entity_end_turn(LwState *state, int entity_idx);

#endif /* LW_TURN_H */
