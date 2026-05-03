/*
 * lw_effect_permutation.c -- 1:1 port of effect/EffectPermutation.java
 */
#include <stddef.h>

#include "lw_effect.h"
#include "lw_state.h"


/* @Override
 * public void apply(State fight) { ... }
 */
void lw_effect_permutation_apply(LwEffect *self, struct LwState *fight) {

    /* Java: fight.invertEntities(caster, target); */
    lw_state_invert_entities(fight, self->caster, self->target);
}
