/*
 * lw_effect_shackle_tp.c -- 1:1 port of effect/EffectShackleTP.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 *
 * Base shackle : base × (1 + magic / 100)
 */
void lw_effect_shackle_tp_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    int caster_magic = lw_entity_get_magic(self->caster);
    if (caster_magic < 0) caster_magic = 0;
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * (1.0 + (double) caster_magic / 100.0)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_TP, -self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_TP, -self->value, self->caster);
    }
}
