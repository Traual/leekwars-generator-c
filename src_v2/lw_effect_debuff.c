/*
 * lw_effect_debuff.c -- 1:1 port of effect/EffectDebuff.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_actions.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_debuff_apply(LwEffect *self, struct LwState *state) {

    /* Java:
     * value = (int) ((value1 + jet * value2) * aoe * criticalPower * targetCount);
     */
    self->value = (int) ((self->value1 + self->jet * self->value2)
                         * self->aoe * self->critical_power * self->target_count);
    lw_entity_reduce_effects(self->target, (double) self->value / 100.0, self->caster);

    /* "Les effets de X sont réduits de Y%" */
    lw_actions_log_reduce_effects(lw_state_get_actions(state), self->target, self->value);
}
