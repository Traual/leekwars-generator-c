/*
 * lw_critical.c -- agility-scaled critical-hit roll, byte-for-byte
 * with leekwars/fight/fight.py::Fight.generateCritical.
 *
 * Python:
 *   return self.state.getRandom().get_double() < (caster.getAgility() / 1000.0)
 *
 * Notes:
 *  - We use base+buff agility (matches getStat at the call site).
 *  - The threshold is NOT clamped: agility >= 1000 always crits, and
 *    negative agility (debuff overflow) never crits since get_double()
 *    is in [0.5, 1.0). Mirrors Python.
 *  - The RNG draw consumes one Java-LCG step; sharing the same
 *    rng_n with jet/order/etc. keeps reproducibility intact.
 */

#include "lw_critical.h"
#include "lw_rng.h"


static int stat(const LwEntity *e, int idx) {
    return e->base_stats[idx] + e->buff_stats[idx];
}


int lw_roll_critical(LwState *state, int caster_idx) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;

    const LwEntity *caster = &state->entities[caster_idx];
    int agility = stat(caster, LW_STAT_AGILITY);

    double roll      = lw_rng_double(&state->rng_n);
    double threshold = (double)agility / 1000.0;
    return roll < threshold ? 1 : 0;
}


double lw_roll_critical_power(LwState *state, int caster_idx) {
    return lw_roll_critical(state, caster_idx) ? LW_CRITICAL_FACTOR : 1.0;
}
