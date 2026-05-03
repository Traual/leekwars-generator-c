/*
 * lw_effect_raw_buff_tp.c -- 1:1 port of effect/EffectRawBuffTP.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_raw_buff_tp_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    /* Java:
     * value = (int) Math.round((value1 + value2 * jet) * targetCount * criticalPower);
     */
    self->value = (int) lw_java_round((self->value1 + self->value2 * self->jet)
                                      * self->target_count * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_TP, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_TP, self->value, self->caster);
    }
}
