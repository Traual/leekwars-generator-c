/*
 * lw_effect_life_damage.c -- 1:1 port of effect/EffectLifeDamage.java
 *
 * Java field on EffectLifeDamage: private int returnDamage = 0;
 * (local to apply() in our C port).
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
void lw_effect_life_damage_apply(LwEffect *self, struct LwState *state) {

    int returnDamage = 0;

    /* Base damages */
    /* Java:
     * double d = ((value1 + jet * value2) / 100) * caster.getLife() * aoe
     *          * criticalPower * (1 + caster.getPower() / 100.0);
     */
    double d = ((self->value1 + self->jet * self->value2) / 100.0)
             * lw_entity_get_life(self->caster) * self->aoe
             * self->critical_power * (1.0 + (double) lw_entity_get_power(self->caster) / 100.0);

    if (lw_entity_has_state(self->target, LW_ENTITY_STATE_INVINCIBLE)) {
        d = 0.0;
    }

    /* Return damage */
    if (self->target != self->caster) {
        returnDamage = (int) lw_java_round(d * lw_entity_get_damage_return(self->target) / 100.0);
    }

    /* Shields */
    d -= d * (lw_entity_get_relative_shield(self->target) / 100.0)
       + lw_entity_get_absolute_shield(self->target);
    if (d < 0.0) d = 0.0;

    self->value = (int) lw_java_round(d);

    if (lw_entity_get_life(self->target) < self->value) {
        self->value = lw_entity_get_life(self->target);
    }

    int erosion = (int) lw_java_round((double) self->value * self->erosion_rate);

    lw_actions_log_damage(lw_state_get_actions(state),
                          LW_DAMAGE_TYPE_LIFE, self->target, self->value, erosion);
    lw_entity_remove_life(self->target, self->value, erosion, self->caster,
                          LW_DAMAGE_TYPE_LIFE, self, lw_effect_get_item(self));
    lw_entity_on_direct_damage(self->target, self->value);
    lw_entity_on_nova_damage(self->target, erosion);

    /* Return damage */
    if (returnDamage > 0 && !lw_entity_has_state(self->caster, LW_ENTITY_STATE_INVINCIBLE)) {

        if (lw_entity_get_life(self->caster) < returnDamage) {
            returnDamage = lw_entity_get_life(self->caster);
        }

        int returnErosion = (int) lw_java_round((double) returnDamage * self->erosion_rate);

        if (returnDamage > 0) {
            lw_actions_log_damage(lw_state_get_actions(state),
                                  LW_DAMAGE_TYPE_RETURN, self->caster,
                                  returnDamage, returnErosion);
            lw_entity_remove_life(self->caster, returnDamage, returnErosion, self->target,
                                  LW_DAMAGE_TYPE_RETURN, self, lw_effect_get_item(self));
        }
    }
}
