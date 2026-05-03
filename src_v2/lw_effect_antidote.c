/*
 * lw_effect_antidote.c -- 1:1 port of effect/EffectAntidote.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_actions.h"
#include "lw_state.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_antidote_apply(LwEffect *self, struct LwState *state) {

    lw_entity_clear_poisons(self->target, self->caster);

    /* "Les poisons de X sont neutralisés" */
    lw_actions_log_remove_poisons(lw_state_get_actions(state), self->target);
}
