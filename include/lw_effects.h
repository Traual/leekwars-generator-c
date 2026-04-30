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

/* Tick path for aftereffect (per-turn re-deal of stored value).
 * Mirrors ``EffectAftereffect.applyStartTurn``. Caps to remaining HP.
 * Returns dealt damage. */
int lw_tick_aftereffect(LwState *state, int target_idx, int per_turn_damage);

/* Tick path for multi-turn heal. Mirrors EffectHeal.applyStartTurn:
 * caps to missing HP, UNHEALABLE -> 0. Returns healed amount. */
int lw_tick_heal(LwState *state, int target_idx, int per_turn_heal);

/* ---------------- shackles (magic-scaled debuffs) --------------- */

/*
 * Generic shackle effect. Used by EffectShackleMP/TP/Strength/Magic/
 * Agility/Wisdom -- formula identical, only the target stat slot
 * differs. Magic is clamped at 0 (matches ``max(0, magic)`` in Python).
 *
 *   v = round((value1 + jet*value2)
 *             * (1 + max(0, caster.magic) / 100)
 *             * aoe * critical_power);
 *   if v > 0: target.buff_stats[stat_index] -= v;
 *
 * Returns the magnitude of the debuff (always >= 0). Note: this is
 * "buff_stats -= v" so it shows up as a negative buff.
 */
int lw_apply_shackle(LwState *state,
                     int caster_idx,
                     int target_idx,
                     int stat_index,
                     double value1,
                     double value2,
                     double jet,
                     double aoe,
                     double critical_power);

/* ---------------- vitality / nova vitality ---------------------- */

/*
 * EffectVitality: wisdom-scaled, bumps target.total_hp AND target.hp by
 * the same value. Returns the gained amount. Clamped at 0.
 */
int lw_apply_vitality(LwState *state,
                      int caster_idx,
                      int target_idx,
                      double value1,
                      double value2,
                      double jet,
                      double aoe,
                      double critical_power);

/*
 * EffectNovaVitality: science-scaled, bumps target.total_hp ONLY.
 * Current HP unchanged. Returns the gained amount.
 */
int lw_apply_nova_vitality(LwState *state,
                           int caster_idx,
                           int target_idx,
                           double value1,
                           double value2,
                           double jet,
                           double aoe,
                           double critical_power);

/* ---------------- raw heal / steal life ------------------------- */

/*
 * EffectRawHeal: heal without wisdom scaling. Capped at missing HP.
 * UNHEALABLE -> 0.
 */
int lw_apply_raw_heal(LwState *state,
                      int caster_idx,
                      int target_idx,
                      double value1,
                      double value2,
                      double jet,
                      double aoe,
                      double critical_power,
                      int    target_count);

/*
 * EffectStealLife: heals target by ``previous_value`` (the amount
 * dealt by the previous damage effect in the same chain). UNHEALABLE
 * -> 0. Caps at missing HP.
 */
int lw_apply_steal_life(LwState *state,
                        int target_idx,
                        int previous_value);

/* ---------------- nova damage / life damage --------------------- */

/*
 * EffectNovaDamage: science+power-scaled, but writes only to total_hp
 * (current HP unchanged). Capped at total_hp - hp (i.e. the "missing
 * HP gap" — total_hp can never drop below current hp this way).
 * INVINCIBLE -> 0. Returns the amount applied.
 */
int lw_apply_nova_damage(LwState *state,
                         int caster_idx,
                         int target_idx,
                         double value1,
                         double value2,
                         double jet,
                         double aoe,
                         double critical_power);

/*
 * EffectLifeDamage: damage scaled by a percentage of caster's CURRENT
 * life. Goes through shields like normal damage. INVINCIBLE -> 0.
 *
 *   d = (value1 + jet*value2) / 100 * caster.life
 *       * aoe * critical_power * (1 + caster.power/100)
 *
 * Plus damage-return reflect: if the target is not the caster and
 * has DamageReturn buff, the caster takes round(d * dmg_return / 100)
 * back (capped at caster's HP).
 *
 * Returns the damage dealt to the primary target (NOT including the
 * reflected damage). The caster's hp is mutated in-place by reflect.
 */
int lw_apply_life_damage(LwState *state,
                         int caster_idx,
                         int target_idx,
                         double value1,
                         double value2,
                         double jet,
                         double aoe,
                         double critical_power);

/* ---------------- debuff / total debuff / antidote -------------- */

/*
 * EffectDebuff: reduce all reducible (non-IRREDUCTIBLE) effects on
 * the target by ``percent`` (where the formula gives an amount that
 * is interpreted as a percentage). The "value" computed is the raw
 * percent number (0..100, possibly larger).
 *
 *   v = floor((value1 + jet*value2) * aoe * crit_power * target_count)
 *   target.reduceEffects(v / 100.0, caster)
 *
 * Returns v (the raw percent value).
 */
int lw_apply_debuff(LwState *state,
                    int caster_idx,
                    int target_idx,
                    double value1,
                    double value2,
                    double jet,
                    double aoe,
                    double critical_power,
                    int    target_count);

/* EffectTotalDebuff: same as Debuff but also strips irreductible
 * buffs. */
int lw_apply_total_debuff(LwState *state,
                          int caster_idx,
                          int target_idx,
                          double value1,
                          double value2,
                          double jet,
                          double aoe,
                          double critical_power,
                          int    target_count);

/* EffectAntidote: removes every poison effect from the target. Returns
 * number of poisons cleared. */
int lw_apply_antidote(LwState *state, int target_idx);

/* ---------------- "raw" buffs (no caster stat scaling) ---------- */

/*
 * Generic "raw" buff: identical to lw_apply_buff_stat except there's
 * no caster-stat scaling -- the formula is just
 *   v = round((value1 + jet*value2) * aoe * critical_power)
 * Used by EffectRawBuffStrength/Magic/Science/Agility/Resistance/
 * Wisdom/MP/TP/Power and by EffectRawAbsoluteShield /
 * EffectRawRelativeShield (any stat slot, all share the same shape).
 *
 * If v > 0: target.buff_stats[stat_index] += v.
 */
int lw_apply_raw_buff_stat(LwState *state,
                           int target_idx,
                           int stat_index,
                           double value1,
                           double value2,
                           double jet,
                           double aoe,
                           double critical_power);

/* ---------------- vulnerabilities (negative shields) ----------- */

/*
 * EffectVulnerability: writes a NEGATIVE delta to the relative shield
 * slot. No caster-stat scaling. Returns the magnitude (positive).
 */
int lw_apply_vulnerability(LwState *state,
                           int target_idx,
                           double value1,
                           double value2,
                           double jet,
                           double aoe,
                           double critical_power);

/*
 * EffectAbsoluteVulnerability: same shape, writes to the absolute
 * shield slot.
 */
int lw_apply_absolute_vulnerability(LwState *state,
                                    int target_idx,
                                    double value1,
                                    double value2,
                                    double jet,
                                    double aoe,
                                    double critical_power);

/* ---------------- kill / add state / steal_absolute_shield ----- */

/*
 * EffectKill: instakill (target.hp = 0, alive = 0). Returns the HP
 * that was lost (i.e. the target's life before the call). Bypasses
 * shields and INVINCIBLE per the Python reference (Graal note in
 * effect_kill.py).
 */
int lw_apply_kill(LwState *state, int caster_idx, int target_idx);

/*
 * EffectAddState: adds a state-flag bit to target.state_flags.
 * Mirrors target.addState(state). The flag must be one of the
 * LW_STATE_* constants from lw_types.h.
 */
int lw_apply_add_state(LwState *state, int target_idx, uint32_t state_flag);

/*
 * EffectStealAbsoluteShield: grants the target an absolute-shield
 * buff equal to ``previous_value`` (the value computed by the previous
 * effect in the chain, typically a damage roll).
 */
int lw_apply_steal_absolute_shield(LwState *state,
                                   int target_idx,
                                   int previous_value);

/* ---------------- shackle cleanup ------------------------------ */

/* EffectRemoveShackles: walks the target's effect list and removes
 * every SHACKLE_* effect. buff_stats unwound automatically. Returns
 * count removed. */
int lw_apply_remove_shackles(LwState *state, int target_idx);

#endif /* LW_EFFECTS_H */
