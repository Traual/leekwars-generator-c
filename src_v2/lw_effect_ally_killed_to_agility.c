/*
 * lw_effect_ally_killed_to_agility.c -- 1:1 port of effect/EffectAllyKilledToAgility.java
 *
 * Java class body is empty (no apply / no applyStartTurn override).
 * The base Effect.apply() = {} and applyStartTurn = {} so dispatch is a
 * no-op.  We still expose the symbol so the dispatcher in lw_effect.c
 * can reference it without an unresolved-name error if a future change
 * promotes the class to non-empty.  Today: nothing to do.
 */
#include <stddef.h>

#include "lw_effect.h"


void lw_effect_ally_killed_to_agility_apply(LwEffect *self, struct LwState *state) {
    (void) self;
    (void) state;
    /* Java body is empty -- no-op. */
}
