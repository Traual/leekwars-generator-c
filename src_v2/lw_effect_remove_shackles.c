/*
 * lw_effect_remove_shackles.c -- 1:1 port of effect/EffectRemoveShackles.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_actions.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_remove_shackles_apply(LwEffect *self, struct LwState *state) {
    lw_entity_remove_shackles(self->target);

    /* "Les entraves de X sont retirées" */
    lw_actions_log_remove_shackles(lw_state_get_actions(state), self->target);
}
