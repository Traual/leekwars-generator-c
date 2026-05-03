/*
 * lw_effect_poison.c -- 1:1 port of effect/EffectPoison.java
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
void lw_effect_poison_apply(LwEffect *self, struct LwState *state) {

    /* Java:
     * value = (int) Math.round(((value1 + jet * value2))
     *           * (1 + (double) Math.max(0, caster.getMagic()) / 100)
     *           * aoe * criticalPower
     *           * (1 + caster.getPower() / 100.0));
     */
    int caster_magic = lw_entity_get_magic(self->caster);
    if (caster_magic < 0) caster_magic = 0;
    self->value = (int) lw_java_round(((self->value1 + self->jet * self->value2))
                                      * (1.0 + (double) caster_magic / 100.0)
                                      * self->aoe * self->critical_power
                                      * (1.0 + (double) lw_entity_get_power(self->caster) / 100.0));
}


/* @Override
 * public void applyStartTurn(State state) { ... }
 */
void lw_effect_poison_apply_start_turn(LwEffect *self, struct LwState *state) {

    int damages = self->value;
    if (lw_entity_get_life(self->target) < damages) {
        damages = lw_entity_get_life(self->target);
    }

    if (lw_entity_has_state(self->target, LW_ENTITY_STATE_INVINCIBLE)) {
        damages = 0;
    }

    if (damages > 0) {
        int erosion = (int) lw_java_round((double) damages * self->erosion_rate);

        lw_actions_log_damage(lw_state_get_actions(state),
                              LW_DAMAGE_TYPE_POISON, self->target, damages, erosion);
        lw_entity_remove_life(self->target, damages, erosion, self->caster,
                              LW_DAMAGE_TYPE_POISON, self, lw_effect_get_item(self));
        lw_entity_on_poison_damage(self->target, damages);
        lw_entity_on_nova_damage(self->target, erosion);
    }
}
