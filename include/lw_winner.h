/*
 * lw_winner.h -- fight-end detection.
 *
 * Returns the winning team id (or sentinel for "no winner / draw").
 * Mirrors leekwars/state/state.py::computeWinner with the chest-hunt
 * mode stripped out (we don't model chest teams yet).
 *
 * Sentinels:
 *   LW_WIN_ONGOING (-1) -- more than one team has alive entities
 *   LW_WIN_DRAW    (-2) -- nobody is alive, OR draw_check_hp resolved
 *                          to a tie at fight-timer expiration
 *
 *   0..N           -- the team_id of the winner
 */
#ifndef LW_WINNER_H
#define LW_WINNER_H

#include "lw_state.h"

#define LW_WIN_ONGOING  (-1)
#define LW_WIN_DRAW     (-2)

/*
 * Compute the winner.
 *
 * If ``draw_check_hp`` is non-zero, ties on alive-team-count are broken
 * by total HP (sum of hp over each team's alive entities). This is
 * what Python triggers at the max-turns boundary.
 *
 * The function does NOT mutate state; the fight loop is expected to
 * write the result into state->winner if it wants to persist it.
 */
int lw_compute_winner(const LwState *state, int draw_check_hp);

#endif /* LW_WINNER_H */
