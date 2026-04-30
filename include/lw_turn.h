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

#endif /* LW_TURN_H */
