/*
 * lw_effect_steal_absolute_shield.c -- 1:1 port of effect/EffectStealAbsoluteShield.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_steal_absolute_shield_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    self->value = self->previous_effect_total_value;
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_ABSOLUTE_SHIELD, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_ABSOLUTE_SHIELD, self->value, self->caster);
    }
}
