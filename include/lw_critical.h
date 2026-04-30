/*
 * lw_critical.h -- critical-hit roll matching the Python engine.
 *
 * Reference: leekwars/fight/fight.py::Fight.generateCritical
 *   return state.getRandom().get_double() < (caster.getAgility() / 1000.0)
 *
 * The roll is performed once per attack use (NOT per target). The
 * resulting boolean is then converted to a multiplicative factor:
 *   critical_power = CRITICAL_FACTOR if critical else 1.0
 * which gets threaded through every effect's apply() formula. Constants
 * mirror leekwars/effect/effect.py.
 *
 * This module owns the RNG draw -- callers must not have already
 * consumed the next get_double() before calling lw_roll_critical, or
 * parity will silently drift.
 */
#ifndef LW_CRITICAL_H
#define LW_CRITICAL_H

#include "lw_state.h"

/* Multiplicative factor on a critical hit. Effect.CRITICAL_FACTOR. */
#define LW_CRITICAL_FACTOR  1.3

/* Erosion bonus when the hit was a critical -- Effect.EROSION_CRITICAL_BONUS.
 * Erosion is not yet ported; expose the constant so the future port
 * doesn't have to dig through Python again. */
#define LW_EROSION_CRITICAL_BONUS  0.10

/*
 * Roll the critical-hit die.
 *
 * Returns 1 if the attack is critical, 0 otherwise. Advances state->rng_n
 * by exactly one get_double() draw. caster_idx out of range -> 0.
 *
 * Threshold uses base+buff agility (matches getStat at the call site
 * in Python).
 */
int lw_roll_critical(LwState *state, int caster_idx);

/*
 * Convenience: roll and return critical_power directly (1.0 or 1.3).
 * Use this at the call site in the upcoming attack-application path.
 */
double lw_roll_critical_power(LwState *state, int caster_idx);

#endif /* LW_CRITICAL_H */
