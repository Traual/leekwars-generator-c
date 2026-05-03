/*
 * lw_effect_raw_relative_shield.c -- 1:1 port of effect/EffectRawRelativeShield.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_raw_relative_shield_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    /* Java:
     * value = (int) Math.round((value1 + jet * value2) * aoe * criticalPower);
     */
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_RELATIVE_SHIELD, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_RELATIVE_SHIELD, self->value, self->caster);
    }
}
