/*
 * lw_effect_nova_vitality.c -- 1:1 port of effect/EffectNovaVitality.java
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
void lw_effect_nova_vitality_apply(LwEffect *self, struct LwState *state) {

    /* Java:
     * value = (int) Math.round((value1 + jet * value2)
     *           * (1 + caster.getScience() / 100.0)
     *           * aoe * criticalPower);
     */
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * (1.0 + (double) lw_entity_get_science(self->caster) / 100.0)
                                      * self->aoe * self->critical_power);

    lw_actions_log_nova_vitality(lw_state_get_actions(state), self->target, self->value);
    lw_entity_add_total_life(self->target, self->value, self->caster);
}
