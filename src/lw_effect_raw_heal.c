/*
 * lw_effect_raw_heal.c -- 1:1 port of effect/EffectRawHeal.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_actions.h"
#include "lw_util.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_raw_heal_apply(LwEffect *self, struct LwState *state) {

    if (lw_entity_has_state(self->target, LW_ENTITY_STATE_UNHEALABLE)) return;

    /* Java:
     * value = (int) Math.round((value1 + jet * value2)
     *           * aoe * criticalPower * targetCount);
     */
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * self->aoe * self->critical_power * self->target_count);

    if (lw_entity_get_life(self->target) + self->value > lw_entity_get_total_life(self->target)) {
        self->value = lw_entity_get_total_life(self->target) - lw_entity_get_life(self->target);
    }
    lw_actions_log_heal(lw_state_get_actions(state), self->target, self->value);
    lw_entity_add_life(self->target, self->caster, self->value);
}
