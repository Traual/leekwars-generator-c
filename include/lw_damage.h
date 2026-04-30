/*
 * lw_damage.h -- byte-for-byte port of Python's EffectDamage.apply.
 *
 * The damage formula is the centrepiece of every weapon and most
 * chips. This header exposes a single function that mirrors the
 * Python implementation at
 * leekwars/effect/effect_damage.py::EffectDamage.apply, with the
 * same arithmetic order so the same RNG sequence (jet) yields the
 * same final HP.
 *
 * Out-of-scope (TODO when we extend the parity port):
 *   - life steal feedback to caster
 *   - return damage feedback to caster
 *   - erosion (max-HP reduction)
 *   - action stream logging (ActionDamage / ActionHeal)
 *   - critical onCritical() side effects
 *
 * For now we focus on the core HP delta + shield application so a
 * solo "weapon X fires at target Y" produces the same target HP.
 */
#ifndef LW_DAMAGE_H
#define LW_DAMAGE_H

#include "lw_state.h"

/*
 * Apply a damage roll exactly the way Python does.
 *
 * Inputs:
 *   state         -- the live state (currently only used to look up
 *                    entities; will be used for action stream once we
 *                    add it).
 *   caster_idx    -- entity index in state->entities (NOT FId).
 *   target_idx    -- same.
 *   value1, value2 -- min / max raw damage from the attack profile.
 *   jet           -- random throw, must come from state->rng_n via
 *                    lw_rng_double() to keep parity.
 *   aoe           -- area-of-effect multiplier (1.0 for single target).
 *   critical_power -- 1.0 normally, Effect.CRITICAL_FACTOR (2.0) on a
 *                    critical hit.
 *   target_count  -- multiplier from MULTIPLIED_BY_TARGETS modifier; 1
 *                    in the most common case.
 *
 * Returns the actual damage dealt (after shields, capped to remaining
 * HP). Mutates ``state->entities[target_idx]``.
 */
int lw_apply_damage(LwState *state,
                    int caster_idx,
                    int target_idx,
                    double value1,
                    double value2,
                    double jet,
                    double aoe,
                    double critical_power,
                    int    target_count);

/*
 * Apply an immediate (turns=0) heal. Mirrors Python's
 * EffectHeal.apply when ``turns == 0``. Returns the actual amount
 * healed (capped to remaining missing HP). UNHEALABLE state -> 0.
 */
int lw_apply_heal(LwState *state,
                  int caster_idx,
                  int target_idx,
                  double value1,
                  double value2,
                  double jet,
                  double aoe,
                  double critical_power,
                  int    target_count);

/*
 * Apply an absolute shield buff. Mirrors EffectAbsoluteShield.apply.
 * Adds to target.buff_stats[ABSOLUTE_SHIELD]. Returns the shield
 * amount granted.
 */
int lw_apply_absolute_shield(LwState *state,
                             int caster_idx,
                             int target_idx,
                             double value1,
                             double value2,
                             double jet,
                             double aoe,
                             double critical_power);

/*
 * Apply a relative (percentage) shield buff. Mirrors
 * EffectRelativeShield.apply.
 */
int lw_apply_relative_shield(LwState *state,
                             int caster_idx,
                             int target_idx,
                             double value1,
                             double value2,
                             double jet,
                             double aoe,
                             double critical_power);

/*
 * Apply max-HP erosion. Mirrors the second arg of Python's
 * Entity.removeLife(pv, erosion, ...): subtracts round(value * rate)
 * from target.total_hp, with a floor of 1. Returns the erosion amount
 * actually applied.
 *
 * ``rate`` comes from Effect.EROSION_DAMAGE (0.05), Effect.EROSION_POISON
 * (0.10), with +Effect.EROSION_CRITICAL_BONUS (0.10) on a crit. Helper
 * lw_erosion_rate computes that combination.
 */
int    lw_apply_erosion(LwState *state, int target_idx,
                        int value, double rate);

double lw_erosion_rate(int effect_type, int is_critical);

#endif /* LW_DAMAGE_H */
