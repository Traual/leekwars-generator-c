/*
 * lw_effect_aftereffect.c -- 1:1 port of effect/EffectAftereffect.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_actions.h"
#include "lw_attack.h"
#include "lw_util.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_aftereffect_apply(LwEffect *self, struct LwState *state) {

    /* Java:
     * value = (int) Math.round((value1 + value2 * jet)
     *           * (1 + (double) caster.getScience() / 100)
     *           * aoe * criticalPower);
     * value = Math.max(0, value);
     */
    self->value = (int) lw_java_round((self->value1 + self->value2 * self->jet)
                                      * (1.0 + (double) lw_entity_get_science(self->caster) / 100.0)
                                      * self->aoe * self->critical_power);
    if (self->value < 0) self->value = 0;

    if (lw_entity_has_state(self->target, LW_ENTITY_STATE_INVINCIBLE)) {
        self->value = 0;
    }

    if (lw_entity_get_life(self->target) < self->value) {
        self->value = lw_entity_get_life(self->target);
    }
    int erosion = (int) lw_java_round((double) self->value * self->erosion_rate);

    lw_actions_log_damage(lw_state_get_actions(state),
                          LW_DAMAGE_TYPE_AFTEREFFECT, self->target, self->value, erosion);
    lw_entity_remove_life(self->target, self->value, erosion, self->caster,
                          LW_DAMAGE_TYPE_AFTEREFFECT, self, lw_effect_get_item(self));
    lw_entity_on_nova_damage(self->target, erosion);
}


/* @Override
 * public void applyStartTurn(State state) { ... }
 */
void lw_effect_aftereffect_apply_start_turn(LwEffect *self, struct LwState *state) {

    if (lw_entity_get_life(self->target) < self->value) {
        self->value = lw_entity_get_life(self->target);
    }
    int erosion = (int) lw_java_round((double) self->value * self->erosion_rate);

    lw_actions_log_damage(lw_state_get_actions(state),
                          LW_DAMAGE_TYPE_AFTEREFFECT, self->target, self->value, erosion);
    lw_entity_remove_life(self->target, self->value, erosion, self->caster,
                          LW_DAMAGE_TYPE_AFTEREFFECT, self, lw_effect_get_item(self));
    lw_entity_on_nova_damage(self->target, erosion);
}
