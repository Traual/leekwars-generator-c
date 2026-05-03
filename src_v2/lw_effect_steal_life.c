/*
 * lw_effect_steal_life.c -- 1:1 port of effect/EffectStealLife.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_actions.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_steal_life_apply(LwEffect *self, struct LwState *state) {

    if (lw_entity_has_state(self->target, LW_ENTITY_STATE_UNHEALABLE)) return;

    self->value = self->previous_effect_total_value;
    if (self->value > 0) {

        if (lw_entity_get_life(self->target) + self->value > lw_entity_get_total_life(self->target)) {
            self->value = lw_entity_get_total_life(self->target) - lw_entity_get_life(self->target);
        }

        lw_actions_log_heal(lw_state_get_actions(state), self->target, self->value);
        lw_entity_add_life(self->target, self->caster, self->value);
    }
}
