/*
 * lw_effect_vitality.c -- 1:1 port of effect/EffectVitality.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_actions.h"
#include "lw_util.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_vitality_apply(LwEffect *self, struct LwState *state) {

    /* Java:
     * value = (int) Math.round((value1 + jet * value2)
     *           * (1 + caster.getWisdom() / 100.0)
     *           * aoe * criticalPower);
     */
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * (1.0 + (double) lw_entity_get_wisdom(self->caster) / 100.0)
                                      * self->aoe * self->critical_power);

    /* Soin negatif si la sagesse est negative */
    if (self->value < 0) self->value = 0;

    lw_actions_log_vitality(lw_state_get_actions(state), self->target, self->value);
    lw_entity_add_total_life(self->target, self->value, self->caster);
    lw_entity_add_life(self->target, self->caster, self->value);
}
