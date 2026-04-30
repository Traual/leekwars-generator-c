/*
 * lw_effect_dispatch.h -- Effect.createEffect glue for the C engine.
 *
 * In Python, Effect.createEffect is the central dispatcher: it picks
 * the right Effect subclass, calls apply(), and registers the
 * resulting object on the target's effect list (with stacking and
 * non-stackable replacement). The C engine's apply_* functions are
 * the equivalent of apply() for each subclass; this header wraps them
 * up under one entry point so callers can address effects by their
 * numeric type id (the way attacks / chips do).
 *
 * Coverage: every effect type for which we have an apply_* function.
 * Effects whose Python class is None (POISON_TO_SCIENCE,
 * DAMAGE_TO_*, NOVA_DAMAGE_TO_MAGIC, ALLY_KILLED_TO_AGILITY,
 * KILL_TO_TP, CRITICAL_TO_HEAL, MOVED_TO_MP) are passive markers --
 * they rely on the action stream / event hooks which we'll add later.
 * For now the dispatcher returns 0 for those.
 *
 * Out of scope: stacking / replacement rules (those need ItemId on
 * effects, plus the action log; later). For now the dispatcher just
 * appends to the effect list when turns > 0.
 */
#ifndef LW_EFFECT_DISPATCH_H
#define LW_EFFECT_DISPATCH_H

#include "lw_state.h"

/* Input to the dispatcher; mirrors the args Effect.createEffect takes
 * in Python. */
typedef struct {
    int    type;            /* LW_EFFECT_* */
    int    caster_idx;
    int    target_idx;
    double value1, value2;
    double jet;
    int    turns;
    double aoe;
    int    critical;        /* 0/1 — caller rolled with lw_roll_critical */
    int    attack_id;       /* item id for stacking; -1 if no attack */
    int    modifiers;       /* LW_MODIFIER_* bits */
    int    previous_value;  /* used by STEAL_LIFE / STEAL_ABSOLUTE_SHIELD */
    int    target_count;    /* MULTIPLIED_BY_TARGETS factor; 1 normally */
} LwEffectInput;

/*
 * Run the apply path for this effect type and (if turns > 0 and value
 * > 0) push an LwEffect entry on target.effects so the start/end-turn
 * machinery can tick it. Returns the computed "headline" value.
 *
 * For passive effect ids (no apply path) returns 0 and creates no
 * entry.
 */
int lw_effect_create(LwState *state, const LwEffectInput *p);

#endif /* LW_EFFECT_DISPATCH_H */
