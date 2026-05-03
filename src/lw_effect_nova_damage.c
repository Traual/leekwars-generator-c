/*
 * lw_effect_nova_damage.c -- 1:1 port of effect/EffectNovaDamage.java
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
void lw_effect_nova_damage_apply(LwEffect *self, struct LwState *state) {

    /* Base damages */
    /* Java:
     * double d = (value1 + jet * value2)
     *          * (1 + Math.max(0, caster.getScience()) / 100.0)
     *          * aoe * criticalPower * (1 + caster.getPower() / 100.0);
     */
    int caster_science = lw_entity_get_science(self->caster);
    if (caster_science < 0) caster_science = 0;
    double d = (self->value1 + self->jet * self->value2)
             * (1.0 + (double) caster_science / 100.0)
             * self->aoe * self->critical_power
             * (1.0 + (double) lw_entity_get_power(self->caster) / 100.0);

    if (lw_entity_has_state(self->target, LW_ENTITY_STATE_INVINCIBLE)) {
        d = 0.0;
    }

    self->value = (int) lw_java_round(d);

    if (self->value > lw_entity_get_total_life(self->target) - lw_entity_get_life(self->target)) {
        self->value = lw_entity_get_total_life(self->target) - lw_entity_get_life(self->target);
    }

    lw_actions_log_damage(lw_state_get_actions(state),
                          LW_DAMAGE_TYPE_NOVA, self->target, self->value, 0);
    lw_entity_remove_life(self->target, 0, self->value, self->caster,
                          LW_DAMAGE_TYPE_NOVA, self, lw_effect_get_item(self));
    lw_entity_on_nova_damage(self->target, self->value);
}
