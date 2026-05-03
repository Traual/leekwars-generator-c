/*
 * lw_effect_raw_buff_magic.c -- 1:1 port of effect/EffectRawBuffMagic.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_raw_buff_magic_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_MAGIC, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_MAGIC, self->value, self->caster);
    }
}
