/*
 * lw_order.h -- start-of-fight turn order computation.
 *
 * Mirrors leekwars/state/start_order.py byte-for-byte. Used once at
 * fight init to fill state->initial_order from entity team_ids and
 * frequencies, consuming RNG draws (same draws the Python reference
 * pulls).
 *
 * Algorithm summary:
 *   1. Group entities by team_id; sort each group by frequency desc.
 *   2. Compute team probability:
 *        p(team) = 1 / (1 + 10^((sum_freq - team_max_freq) / 100))
 *      then normalize. Sum-of-probabilities = 1 after norm.
 *   3. Pick teams in order by weighted-random walk (one rng draw per
 *      team picked). After each pick, re-normalize the remaining
 *      probabilities.
 *   4. Round-robin entities from each team in the picked order (one
 *      entity per team per round, keep going until all consumed).
 */
#ifndef LW_ORDER_H
#define LW_ORDER_H

#include "lw_state.h"

/*
 * Fill state->initial_order with the entities' starting order.
 *
 * Consumes ``T`` rng draws where T = number of distinct teams that
 * have at least one entity. After this call:
 *   state->initial_order[0..n_in_order-1] = entity_ids in turn order
 *   state->n_in_order                     = total entity count
 *   state->order_index                    = 0
 */
void lw_compute_start_order(LwState *state);

#endif /* LW_ORDER_H */
