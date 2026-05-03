/*
 * lw_effect_kill.c -- 1:1 port of effect/EffectKill.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_actions.h"
#include "lw_attack.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_kill_apply(LwEffect *self, struct LwState *state) {

    /* if (!target.hasState(EntityState.INVINCIBLE)) { // Graal */

    /* Java (commented invincible-check kept for traceability):
     *   value = target.getLife();
     *   state.log(new ActionKill(caster, target));
     *   target.removeLife(value, 0, caster, DamageType.DIRECT, this, getItem());
     * }
     */
    self->value = lw_entity_get_life(self->target);
    lw_actions_log_kill(lw_state_get_actions(state), self->caster, self->target);
    lw_entity_remove_life(self->target, self->value, 0, self->caster,
                          LW_DAMAGE_TYPE_DIRECT, self, lw_effect_get_item(self));
    /* } */
}
