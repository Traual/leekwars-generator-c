/*
 * lw_effect_add_state.c -- 1:1 port of effect/EffectAddState.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_add_state_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    /* Java:
     *   this.value = (int) value1;
     *   this.state = EntityState.values()[(int) value1];
     *   target.addState(this.state);
     *
     * In C, EntityState is collapsed into LW_ENTITY_STATE_* ints; the
     * "ordinal" of the enum (0..11) equals the LW_ENTITY_STATE_* value.
     */
    self->value = (int) self->value1;
    self->state = (int) self->value1;
    lw_entity_add_state(self->target, self->state);
}
