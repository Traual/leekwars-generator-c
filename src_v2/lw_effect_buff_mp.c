/*
 * lw_effect_buff_mp.c -- 1:1 port of effect/EffectBuffMP.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State fight) { ... }    -- Java arg is named `fight` here
 */
void lw_effect_buff_mp_apply(LwEffect *self, struct LwState *fight) {
    (void) fight;

    self->value = (int) lw_java_round((self->value1 + self->value2 * self->jet)
                                      * (1.0 + (double) lw_entity_get_science(self->caster) / 100.0)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_MP, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_MP, self->value, self->caster);
    }
}
