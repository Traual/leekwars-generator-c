/*
 * lw_effect_shackle_magic.c -- 1:1 port of effect/EffectShackleMagic.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State fight) { ... }    -- Java arg is named `fight` here
 *
 * Base shackle : base × (1 + magic / 100)
 */
void lw_effect_shackle_magic_apply(LwEffect *self, struct LwState *fight) {
    (void) fight;

    int caster_magic = lw_entity_get_magic(self->caster);
    if (caster_magic < 0) caster_magic = 0;
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * (1.0 + (double) caster_magic / 100.0)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_MAGIC, -self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_MAGIC, -self->value, self->caster);
    }
}
