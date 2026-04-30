/*
 * lw_effects.h -- catalog of effect-application primitives.
 *
 * Each function ports one ``EffectXXX.apply()`` from the Python
 * engine byte-for-byte. They mutate state in place; in particular,
 * buff effects update target.buff_stats[stat], heal/damage update HP,
 * poison records the per-turn damage value (applied by tick later).
 *
 * For ports that need a turn counter (poison, multi-turn heal, etc.)
 * we'll add a small effect-tick framework once the catalog is wide
 * enough to motivate it.
 */
#ifndef LW_EFFECTS_H
#define LW_EFFECTS_H

#include "lw_state.h"

/* ---------------- buff effects (science-scaled) ----------------- */

/*
 * Generic stat buff. Used by:
 *   EffectBuffStrength / Agility / Wisdom / Magic / Resistance /
 *   MP / TP -- all science-scaled (pass scale_stat=LW_STAT_SCIENCE).
 *   EffectDamageReturn -- agility-scaled (LW_STAT_AGILITY).
 *
 * Formula matches Python:
 *   value = round((value1 + value2 * jet)
 *                 * (1 + caster.<scale_stat> / 100.0)
 *                 * aoe * critical_power);
 *   if value > 0: target.buff_stats[stat_index] += value;
 *
 * ``stat_index`` is the slot to write (LW_STAT_STRENGTH etc.).
 * ``scale_stat`` is the caster stat that scales the magnitude.
 */
int lw_apply_buff_stat(LwState *state,
                       int caster_idx,
                       int target_idx,
                       int stat_index,
                       int scale_stat,
                       double value1,
                       double value2,
                       double jet,
                       double aoe,
                       double critical_power);

/*
 * Aftereffect: damage applied immediately on cast (like Damage but
 * science-scaled instead of strength-scaled), then per-turn while
 * active. Mirrors EffectAftereffect.apply (immediate part).
 *
 * NOTE: the per-turn tick is the same as direct damage tick (just
 * subtract the precomputed value from HP). We expose the immediate
 * apply here; the tick is deferred to the effect framework once we
 * add multi-turn ticking.
 */
int lw_apply_aftereffect(LwState *state,
                          int caster_idx,
                          int target_idx,
                          double value1,
                          double value2,
                          double jet,
                          double aoe,
                          double critical_power);

/* ---------------- damage-over-time (poison) --------------------- */

/*
 * Compute and store the per-turn poison damage value. Mirrors
 * ``EffectPoison.apply``. Magic-scaled, power-scaled, AoE/crit
 * applied. Returns the per-turn damage that will be dealt; caller
 * should add an entry to target.effects with this value and the
 * given turn count, then ``lw_tick_poison`` will deal it each
 * applyStartTurn.
 */
int lw_compute_poison_damage(const LwState *state,
                             int caster_idx,
                             int target_idx,
                             double value1,
                             double value2,
                             double jet,
                             double aoe,
                             double critical_power);

/*
 * Tick a poison effect (deal one turn's worth of poison damage).
 * Mirrors ``EffectPoison.applyStartTurn``. Caps to remaining HP,
 * INVINCIBLE -> 0. Returns dealt damage.
 *
 * The effect entry must have been created previously with
 * lw_compute_poison_damage's output stored in effect->value, and
 * effect->target_id pointing at the right entity.
 */
int lw_tick_poison(LwState *state, int target_idx, int per_turn_damage);

#endif /* LW_EFFECTS_H */
