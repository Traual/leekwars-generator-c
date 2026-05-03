/*
 * lw_effect_buff_agility.c -- 1:1 port of effect/EffectBuffAgility.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_buff_agility_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    /* Java:
     * value = (int) Math.round((value1 + value2 * jet)
     *           * (1 + (double) caster.getScience() / 100)
     *           * aoe * criticalPower);
     */
    self->value = (int) lw_java_round((self->value1 + self->value2 * self->jet)
                                      * (1.0 + (double) lw_entity_get_science(self->caster) / 100.0)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_AGILITY, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_AGILITY, self->value, self->caster);
    }
}
