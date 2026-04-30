/*
 * lw_effect_store.h -- entity effect-list manipulation.
 *
 * Multi-turn effects are stored on the target entity in a fixed-size
 * array (entity.effects[LW_MAX_EFFECTS]). This header is the small
 * lifecycle API:
 *
 *   - lw_effect_init: zero an LwEffect struct + set sensible defaults.
 *   - lw_effect_add: append an effect to a target's list. Returns the
 *     slot index, or -1 if the entity's list is full.
 *   - lw_effect_remove: remove the slot at idx, undoing its stat[]
 *     deltas against target.buff_stats[]. Compacts the array.
 *   - lw_effect_remove_by_id: remove the FIRST effect of the given
 *     type+caster+attack triple. Mirrors Python's "non-stackable
 *     replacement" rule in createEffect.
 *   - lw_effect_clear_poisons: matches EffectAntidote.apply -- drop
 *     every poison effect on the target (buff_stats unaffected since
 *     poison doesn't store stats[]).
 *   - lw_effect_decrement_turns: at end-of-turn, turns--; remove any
 *     effect that hit 0.
 *   - lw_effect_reduce, lw_effect_reduce_total: matches Effect.reduce
 *     for EffectDebuff / EffectTotalDebuff. Scales effect.value and
 *     associated stat[] deltas down by (1 - percent), reapplying the
 *     delta to target.buff_stats[]. "_total" version also handles
 *     irreductible-flagged effects.
 */
#ifndef LW_EFFECT_STORE_H
#define LW_EFFECT_STORE_H

#include "lw_effect.h"
#include "lw_state.h"

/* Initialize an effect struct with defaults that match
 * Effect.__init__ (caster_id/target_id/attack_id all -1, turns 0,
 * critical_power 1.0, aoe 1.0). */
void lw_effect_init(LwEffect *e);

/* Append an effect to target.effects. Returns the slot index, or -1
 * if the entity's effect list is full. The stats[] inside the effect
 * are NOT applied here -- callers are expected to have already pushed
 * those deltas into target.buff_stats[] via the lw_apply_*() helpers. */
int  lw_effect_add(LwEntity *target, const LwEffect *src);

/* Remove the effect at idx. Subtracts effect.stats[] from
 * target.buff_stats[] (so a buff vanishes cleanly). Compacts the list. */
void lw_effect_remove(LwEntity *target, int idx);

/* Remove the first effect of type ``id`` whose caster_id == caster_id
 * AND attack_id == attack_id. Returns 1 if removed, 0 otherwise.
 * Used by createEffect's non-stackable-replacement rule. */
int  lw_effect_remove_by_id(LwEntity *target,
                            int id,
                            int caster_id,
                            int attack_id);

/* Clear every poison effect on the target. Mirrors
 * EffectAntidote.apply -> target.clearPoisons. */
int  lw_effect_clear_poisons(LwEntity *target);

/* End-of-turn: decrement turns on every effect; remove any that hit 0.
 * Returns the number of effects removed. */
int  lw_effect_decrement_turns(LwEntity *target);

/* EffectDebuff path: scale every reducible effect on the target by
 * factor = max(0, 1 - percent). buff_stats[] is updated accordingly.
 * Skips effects flagged LW_MODIFIER_IRREDUCTIBLE (matches Python). */
void lw_effect_reduce(LwEntity *target, double percent);

/* EffectTotalDebuff: same as reduce but ignores LW_MODIFIER_IRREDUCTIBLE
 * and reduces every effect including buffs flagged irreductible. */
void lw_effect_reduce_total(LwEntity *target, double percent);

#endif /* LW_EFFECT_STORE_H */
