/*
 * lw_effect_heal.c -- 1:1 port of effect/EffectHeal.java
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
void lw_effect_heal_apply(LwEffect *self, struct LwState *state) {

    /* Java:
     * value = (int) Math.round((value1 + jet * value2)
     *           * (1 + (double) caster.getWisdom() / 100)
     *           * aoe * criticalPower * targetCount);
     */
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * (1.0 + (double) lw_entity_get_wisdom(self->caster) / 100.0)
                                      * self->aoe * self->critical_power * self->target_count);

    /* Soin negatif si la sagesse est negative */
    if (self->value < 0) self->value = 0;

    if (self->turns == 0) {

        if (lw_entity_has_state(self->target, LW_ENTITY_STATE_UNHEALABLE)) return;

        if (lw_entity_get_life(self->target) + self->value > lw_entity_get_total_life(self->target)) {
            self->value = lw_entity_get_total_life(self->target) - lw_entity_get_life(self->target);
        }
        lw_actions_log_heal(lw_state_get_actions(state), self->target, self->value);
        lw_entity_add_life(self->target, self->caster, self->value);
    }
}


/* @Override
 * public void applyStartTurn(State state) { ... }
 */
void lw_effect_heal_apply_start_turn(LwEffect *self, struct LwState *state) {

    if (lw_entity_has_state(self->target, LW_ENTITY_STATE_UNHEALABLE)) return;

    int life = self->value;
    if (lw_entity_get_life(self->target) + life > lw_entity_get_total_life(self->target)) {
        life = lw_entity_get_total_life(self->target) - lw_entity_get_life(self->target);
    }
    lw_actions_log_heal(lw_state_get_actions(state), self->target, life);
    lw_entity_add_life(self->target, self->caster, life);
}
