/*
 * lw_effect_dispatch.c -- Effect.createEffect porting.
 *
 * The dispatch is a big switch keyed on effect_type. Each branch:
 *   1. Calls the appropriate lw_apply_* function.
 *   2. Captures the returned value.
 *   3. If turns > 0 and value > 0, builds an LwEffect entry with
 *      enough metadata for tick + unwind, and adds it to
 *      target.effects via lw_effect_add.
 *
 * The entry's stats[] field records the per-stat delta applied to
 * target.buff_stats[] so removal via lw_effect_remove unwinds cleanly.
 * For non-buff effects (damage, heal, poison) stats[] stays zero.
 *
 * Effects that don't have an Effect class in Python (id 33-36, 50,
 * 55, 56, 58) are passive markers; we leave them as no-ops here and
 * (later) hook into the action-stream events.
 */

#include "lw_effect_dispatch.h"
#include "lw_effect.h"
#include "lw_effect_store.h"
#include "lw_effects.h"
#include "lw_damage.h"
#include "lw_movement.h"
#include "lw_critical.h"
#include <math.h>


/* Pre-apply replacement: if the effect is non-stackable (Python's
 * "if not stackable" branch in createEffect), look for an existing
 * effect on target with the same (id, attack_id) and remove it.
 * Mirrors the first block of Effect.createEffect.
 *
 * NOTE Python compares by ``e.attack.getItemId()`` so attack_id == -1
 * (no attack) is treated as a separate group from any positive id.
 */
static void replace_existing_non_stackable(LwState *state,
                                            const LwEffectInput *p,
                                            int effect_type) {
    if (p->turns == 0) return;
    if ((p->modifiers & LW_MODIFIER_STACKABLE) != 0) return;
    if (p->target_idx < 0 || p->target_idx >= state->n_entities) return;

    LwEntity *target = &state->entities[p->target_idx];
    for (int i = 0; i < target->n_effects; i++) {
        LwEffect *e = &target->effects[i];
        if (e->id == effect_type && e->attack_id == p->attack_id) {
            lw_effect_remove(target, i);
            return;
        }
    }
}


/* Post-apply stacking: if there's an existing same-(id, turns,
 * attack_id, caster) effect, merge `value` into it via
 * Effect.mergeWith semantics:
 *   e.value += value;
 *   for each non-zero stat slot in e.stats[]: e.stats[k] += value*sign
 * Returns 1 if merged (caller skips the new-entry add), 0 otherwise. */
static int try_stack(LwState *state, const LwEffectInput *p,
                     int effect_type, int value,
                     int stat_index, int stat_sign) {
    if (value <= 0 || p->turns == 0) return 0;
    if (p->target_idx < 0 || p->target_idx >= state->n_entities) return 0;

    LwEntity *target = &state->entities[p->target_idx];
    for (int i = 0; i < target->n_effects; i++) {
        LwEffect *e = &target->effects[i];
        if (e->id        != effect_type)      continue;
        if (e->attack_id != p->attack_id)     continue;
        if (e->turns     != p->turns)         continue;
        if (e->caster_id != p->caster_idx)    continue;

        e->value += value;
        /* mergeWith only updates the stats[] slots that already had a
         * non-zero entry. We honor stat_index/stat_sign for
         * single-slot effects (every buff/shackle/shield in our
         * catalog), which matches Python's loop-over-non-zero-stats
         * behavior because exactly one slot is non-zero per port. */
        if (stat_index >= 0 && stat_index < LW_STAT_COUNT
            && e->stats[stat_index] != 0) {
            e->stats[stat_index] += value * stat_sign;
        }
        return 1;
    }
    return 0;
}


/* Build an effect entry from the input + computed value, then add it
 * to target's effect list. ``stat_index`` is the stat slot whose
 * buff_stats[] was modified (or -1 if none). ``stat_sign`` is +1 / -1
 * to record the sign of the delta.
 *
 * Now also implements Python's stacking rule: before adding, check
 * for an existing same-attack-same-id-same-turns-same-caster effect
 * and merge into it instead.
 */
static void register_entry(LwState *state, const LwEffectInput *p,
                           int value, int effect_type,
                           int stat_index, int stat_sign) {
    if (p->turns <= 0 || value <= 0) return;
    if (p->target_idx < 0 || p->target_idx >= state->n_entities) return;

    /* Try stacking first (matches Python's order: stacking check runs
     * before the new-entry add). */
    if (try_stack(state, p, effect_type, value, stat_index, stat_sign)) {
        return;
    }

    double crit_power = p->critical ? LW_CRITICAL_FACTOR : 1.0;
    LwEffect e;
    lw_effect_init(&e);
    e.id = effect_type;
    e.turns = p->turns;
    e.value = value;
    e.value1 = p->value1;
    e.value2 = p->value2;
    e.aoe = p->aoe;
    e.critical = p->critical;
    e.critical_power = crit_power;
    e.jet = p->jet;
    e.caster_id = p->caster_idx;
    e.target_id = p->target_idx;
    e.attack_id = p->attack_id;
    e.modifiers = p->modifiers;
    if (stat_index >= 0 && stat_index < LW_STAT_COUNT) {
        e.stats[stat_index] = value * stat_sign;
    }
    lw_effect_add(&state->entities[p->target_idx], &e);
}


int lw_effect_create(LwState *state, const LwEffectInput *p) {
    if (state == NULL || p == NULL) return 0;
    double crit_power = p->critical ? LW_CRITICAL_FACTOR : 1.0;
    int    tc         = p->target_count > 0 ? p->target_count : 1;
    int    value      = 0;

    /* Python's first block in createEffect: if turns != 0 and not
     * stackable, remove the existing same-(id, attack_id) effect
     * before computing the new one. This rebuilds buff_stats[] (via
     * effect_remove), so apply() reads the freshly-cleared buffs. */
    replace_existing_non_stackable(state, p, p->type);

    switch (p->type) {

        /* ---------- damage ---------- */
        case LW_EFFECT_DAMAGE: {
            /* Erosion is now applied inside lw_apply_damage_v2 to
             * match Python's EffectDamage.apply byte-for-byte
             * (target removeLife + caster removeLife on returnDamage). */
            double rate = lw_erosion_rate(LW_EFFECT_DAMAGE, p->critical);
            return lw_apply_damage_v2(state, p->caster_idx, p->target_idx,
                                      p->value1, p->value2, p->jet,
                                      p->aoe, crit_power, tc, rate);
        }

        case LW_EFFECT_HEAL:
            /* Immediate heal only when turns == 0; for multi-turn we
             * defer the heal to lw_tick_heal via the entry. */
            if (p->turns == 0) {
                value = lw_apply_heal(state, p->caster_idx, p->target_idx,
                                      p->value1, p->value2, p->jet,
                                      p->aoe, crit_power, tc);
            } else {
                /* Compute per-turn heal value using the same formula
                 * but don't apply yet -- per-turn ticks consume it. */
                double v = (p->value1 + p->jet * p->value2);
                int wisdom = state->entities[p->caster_idx].base_stats[LW_STAT_WISDOM]
                           + state->entities[p->caster_idx].buff_stats[LW_STAT_WISDOM];
                v *= (1.0 + (double)wisdom / 100.0)
                   * p->aoe * crit_power * (double)tc;
                value = (int)floor(v + 0.5);
                if (value < 0) value = 0;
            }
            register_entry(state, p, value, LW_EFFECT_HEAL, -1, 0);
            return value;

        case LW_EFFECT_POISON: {
            value = lw_compute_poison_damage(state, p->caster_idx, p->target_idx,
                                             p->value1, p->value2, p->jet,
                                             p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_POISON, -1, 0);
            return value;
        }

        case LW_EFFECT_AFTEREFFECT:
            value = lw_apply_aftereffect(state, p->caster_idx, p->target_idx,
                                         p->value1, p->value2, p->jet,
                                         p->aoe, crit_power);
            if (value > 0) {
                lw_apply_erosion(state, p->target_idx, value,
                                 lw_erosion_rate(LW_EFFECT_DAMAGE, p->critical));
            }
            register_entry(state, p, value, LW_EFFECT_AFTEREFFECT, -1, 0);
            return value;

        case LW_EFFECT_LIFE_DAMAGE:
            value = lw_apply_life_damage(state, p->caster_idx, p->target_idx,
                                         p->value1, p->value2, p->jet,
                                         p->aoe, crit_power);
            return value;

        case LW_EFFECT_NOVA_DAMAGE:
            value = lw_apply_nova_damage(state, p->caster_idx, p->target_idx,
                                         p->value1, p->value2, p->jet,
                                         p->aoe, crit_power);
            return value;

        /* ---------- shields ---------- */
        case LW_EFFECT_ABSOLUTE_SHIELD:
            value = lw_apply_absolute_shield(state, p->caster_idx, p->target_idx,
                                             p->value1, p->value2, p->jet,
                                             p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_ABSOLUTE_SHIELD,
                           LW_STAT_ABSOLUTE_SHIELD, +1);
            return value;

        case LW_EFFECT_RELATIVE_SHIELD:
            value = lw_apply_relative_shield(state, p->caster_idx, p->target_idx,
                                             p->value1, p->value2, p->jet,
                                             p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_RELATIVE_SHIELD,
                           LW_STAT_RELATIVE_SHIELD, +1);
            return value;

        case LW_EFFECT_RAW_ABSOLUTE_SHIELD:
            value = lw_apply_raw_buff_stat(state, p->target_idx,
                                            LW_STAT_ABSOLUTE_SHIELD,
                                            p->value1, p->value2, p->jet,
                                            p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_RAW_ABSOLUTE_SHIELD,
                           LW_STAT_ABSOLUTE_SHIELD, +1);
            return value;

        case LW_EFFECT_RAW_RELATIVE_SHIELD:
            value = lw_apply_raw_buff_stat(state, p->target_idx,
                                            LW_STAT_RELATIVE_SHIELD,
                                            p->value1, p->value2, p->jet,
                                            p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_RAW_RELATIVE_SHIELD,
                           LW_STAT_RELATIVE_SHIELD, +1);
            return value;

        /* ---------- buffs (science-scaled) ---------- */
#define BUFF_CASE(EFFECT_ID, STAT_SLOT) \
        case EFFECT_ID: \
            value = lw_apply_buff_stat(state, p->caster_idx, p->target_idx, \
                                       STAT_SLOT, LW_STAT_SCIENCE, \
                                       p->value1, p->value2, p->jet, \
                                       p->aoe, crit_power); \
            register_entry(state, p, value, EFFECT_ID, STAT_SLOT, +1); \
            return value
        BUFF_CASE(LW_EFFECT_BUFF_STRENGTH,   LW_STAT_STRENGTH);
        BUFF_CASE(LW_EFFECT_BUFF_AGILITY,    LW_STAT_AGILITY);
        BUFF_CASE(LW_EFFECT_BUFF_MP,         LW_STAT_MP);
        BUFF_CASE(LW_EFFECT_BUFF_TP,         LW_STAT_TP);
        BUFF_CASE(LW_EFFECT_BUFF_RESISTANCE, LW_STAT_RESISTANCE);
        BUFF_CASE(LW_EFFECT_BUFF_WISDOM,     LW_STAT_WISDOM);
#undef BUFF_CASE

        case LW_EFFECT_DAMAGE_RETURN:
            value = lw_apply_buff_stat(state, p->caster_idx, p->target_idx,
                                        LW_STAT_DAMAGE_RETURN, LW_STAT_AGILITY,
                                        p->value1, p->value2, p->jet,
                                        p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_DAMAGE_RETURN,
                           LW_STAT_DAMAGE_RETURN, +1);
            return value;

        /* ---------- raw buffs (no caster-stat scaling) ---------- */
        /* Python's EffectRawBuffMP / EffectRawBuffTP have a different
         * formula: they use ``targetCount * criticalPower`` as the
         * multiplier, NOT ``aoe * criticalPower`` like the other raw
         * buffs. So the dispatcher passes tc (instead of aoe) for
         * those two cases. */
#define RAW_BUFF_AOE_CASE(EFFECT_ID, STAT_SLOT) \
        case EFFECT_ID: \
            value = lw_apply_raw_buff_stat(state, p->target_idx, STAT_SLOT, \
                                           p->value1, p->value2, p->jet, \
                                           p->aoe, crit_power); \
            register_entry(state, p, value, EFFECT_ID, STAT_SLOT, +1); \
            return value
#define RAW_BUFF_TC_CASE(EFFECT_ID, STAT_SLOT) \
        case EFFECT_ID: \
            value = lw_apply_raw_buff_stat(state, p->target_idx, STAT_SLOT, \
                                           p->value1, p->value2, p->jet, \
                                           (double)tc, crit_power); \
            register_entry(state, p, value, EFFECT_ID, STAT_SLOT, +1); \
            return value
        RAW_BUFF_TC_CASE(LW_EFFECT_RAW_BUFF_MP,         LW_STAT_MP);
        RAW_BUFF_TC_CASE(LW_EFFECT_RAW_BUFF_TP,         LW_STAT_TP);
        RAW_BUFF_AOE_CASE(LW_EFFECT_RAW_BUFF_STRENGTH,   LW_STAT_STRENGTH);
        RAW_BUFF_AOE_CASE(LW_EFFECT_RAW_BUFF_MAGIC,      LW_STAT_MAGIC);
        RAW_BUFF_AOE_CASE(LW_EFFECT_RAW_BUFF_SCIENCE,    LW_STAT_SCIENCE);
        RAW_BUFF_AOE_CASE(LW_EFFECT_RAW_BUFF_AGILITY,    LW_STAT_AGILITY);
        RAW_BUFF_AOE_CASE(LW_EFFECT_RAW_BUFF_RESISTANCE, LW_STAT_RESISTANCE);
        RAW_BUFF_AOE_CASE(LW_EFFECT_RAW_BUFF_WISDOM,     LW_STAT_WISDOM);
        RAW_BUFF_AOE_CASE(LW_EFFECT_RAW_BUFF_POWER,      LW_STAT_POWER);
#undef RAW_BUFF_AOE_CASE
#undef RAW_BUFF_TC_CASE

        /* ---------- shackles (magic-scaled, negative) ---------- */
#define SHACKLE_CASE(EFFECT_ID, STAT_SLOT) \
        case EFFECT_ID: \
            value = lw_apply_shackle(state, p->caster_idx, p->target_idx, \
                                     STAT_SLOT, p->value1, p->value2, p->jet, \
                                     p->aoe, crit_power); \
            register_entry(state, p, value, EFFECT_ID, STAT_SLOT, -1); \
            return value
        SHACKLE_CASE(LW_EFFECT_SHACKLE_MP,       LW_STAT_MP);
        SHACKLE_CASE(LW_EFFECT_SHACKLE_TP,       LW_STAT_TP);
        SHACKLE_CASE(LW_EFFECT_SHACKLE_STRENGTH, LW_STAT_STRENGTH);
        SHACKLE_CASE(LW_EFFECT_SHACKLE_MAGIC,    LW_STAT_MAGIC);
        SHACKLE_CASE(LW_EFFECT_SHACKLE_AGILITY,  LW_STAT_AGILITY);
        SHACKLE_CASE(LW_EFFECT_SHACKLE_WISDOM,   LW_STAT_WISDOM);
#undef SHACKLE_CASE

        /* ---------- vulnerabilities (negative shields, no scaling) - */
        case LW_EFFECT_VULNERABILITY:
            value = lw_apply_vulnerability(state, p->target_idx,
                                            p->value1, p->value2, p->jet,
                                            p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_VULNERABILITY,
                           LW_STAT_RELATIVE_SHIELD, -1);
            return value;

        case LW_EFFECT_ABSOLUTE_VULNERABILITY:
            value = lw_apply_absolute_vulnerability(state, p->target_idx,
                                                    p->value1, p->value2, p->jet,
                                                    p->aoe, crit_power);
            register_entry(state, p, value, LW_EFFECT_ABSOLUTE_VULNERABILITY,
                           LW_STAT_ABSOLUTE_SHIELD, -1);
            return value;

        /* ---------- vitality / nova vitality / raw heal / steal --- */
        case LW_EFFECT_VITALITY:
            return lw_apply_vitality(state, p->caster_idx, p->target_idx,
                                     p->value1, p->value2, p->jet,
                                     p->aoe, crit_power);

        case LW_EFFECT_NOVA_VITALITY:
            return lw_apply_nova_vitality(state, p->caster_idx, p->target_idx,
                                          p->value1, p->value2, p->jet,
                                          p->aoe, crit_power);

        case LW_EFFECT_RAW_HEAL:
            return lw_apply_raw_heal(state, p->caster_idx, p->target_idx,
                                     p->value1, p->value2, p->jet,
                                     p->aoe, crit_power, tc);

        case LW_EFFECT_STEAL_LIFE:
            return lw_apply_steal_life(state, p->target_idx, p->previous_value);

        case LW_EFFECT_STEAL_ABSOLUTE_SHIELD:
            return lw_apply_steal_absolute_shield(state, p->target_idx,
                                                   p->previous_value);

        /* ---------- debuffs / antidote / remove_shackles ---------- */
        case LW_EFFECT_DEBUFF:
            return lw_apply_debuff(state, p->caster_idx, p->target_idx,
                                   p->value1, p->value2, p->jet,
                                   p->aoe, crit_power, tc);

        case LW_EFFECT_TOTAL_DEBUFF:
            return lw_apply_total_debuff(state, p->caster_idx, p->target_idx,
                                         p->value1, p->value2, p->jet,
                                         p->aoe, crit_power, tc);

        case LW_EFFECT_ANTIDOTE:
            return lw_apply_antidote(state, p->target_idx);

        case LW_EFFECT_REMOVE_SHACKLES:
            return lw_apply_remove_shackles(state, p->target_idx);

        /* ---------- kill / add_state / multiply ---------- */
        case LW_EFFECT_KILL:
            return lw_apply_kill(state, p->caster_idx, p->target_idx);

        case LW_EFFECT_ADD_STATE:
            /* value1 holds the state-flag bit. Python uses int(value1). */
            return lw_apply_add_state(state, p->target_idx,
                                      (uint32_t)(int)p->value1);

        case LW_EFFECT_MULTIPLY_STATS:
            return lw_apply_multiply_stats(state, p->caster_idx, p->target_idx,
                                            p->value1);

        case LW_EFFECT_RESURRECT:
            /* The dispatcher doesn't know the destination cell here;
             * the attack-application path needs to call lw_apply_resurrect
             * directly with the chosen cell. Return 0 as a no-op. */
            return 0;

        /* ---------- movement effects ---------- */
        case LW_EFFECT_TELEPORT:
            /* The dispatcher doesn't know the destination cell; the
             * attack-application path needs to call lw_apply_teleport
             * directly. Return 0 here as a no-op marker. */
            return 0;

        case LW_EFFECT_PERMUTATION:
            return lw_apply_permutation(state, p->caster_idx, p->target_idx);

        case LW_EFFECT_PUSH:
        case LW_EFFECT_REPEL:
        case LW_EFFECT_ATTRACT:
            /* Same caveat as TELEPORT -- destination is computed from
             * the geometry the attack-application path knows about. */
            return 0;

        default:
            /* Passive markers (33-36, 50, 55, 56, 58) and any other
             * unknown id: no-op. */
            return 0;
    }
}
