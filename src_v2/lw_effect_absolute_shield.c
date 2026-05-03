/*
 * lw_effect_absolute_shield.c -- 1:1 port of effect/EffectAbsoluteShield.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_absolute_shield_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    /* Java:
     * value = (int) Math.round((value1 + jet * value2)
     *           * (1 + caster.getResistance() / 100.0)
     *           * aoe * criticalPower);
     */
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * (1.0 + (double) lw_entity_get_resistance(self->caster) / 100.0)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_ABSOLUTE_SHIELD, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_ABSOLUTE_SHIELD, self->value, self->caster);
    }
}
