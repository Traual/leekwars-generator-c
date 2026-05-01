/*
 * lw_attack_apply.h -- byte-for-byte attack execution.
 *
 * Mirrors leekwars/attack/attack.py::Attack.applyOnCell with the
 * Critical roll happening BEFORE applyOnCell (matches Fight.useWeapon /
 * Fight.useChip).
 *
 * Pipeline:
 *   1. Roll critical (lw_roll_critical -- consumes 1 RNG draw).
 *   2. Roll jet     (lw_rng_double  -- consumes 1 RNG draw). Order
 *      matches Python: critical THEN jet.
 *   3. Enumerate target cells via lw_area_get_cells.
 *   4. Build the target entity list (alive only).
 *   5. For each effect in the attack:
 *        - Compute multiplied_by_targets count if requested
 *        - For each target that passes the target filter:
 *            - Compute aoe_factor (1 - dist * 0.2 on mask shapes; 1.0
 *              on line/first-in-line/allies/enemies/single-cell).
 *            - Dispatch via lw_effect_create.
 *            - Accumulate previous_value for STEAL_LIFE / STEAL_ABS.
 *   6. ATTRACT / PUSH / REPEL / TELEPORT short-circuit through the
 *      lw_apply_slide / lw_apply_teleport primitives instead of going
 *      through lw_effect_create (matching Python's special-case in
 *      attack.py).
 *
 * Returns total damage dealt (positive). Heals are not subtracted.
 */
#ifndef LW_ATTACK_APPLY_H
#define LW_ATTACK_APPLY_H

#include "lw_state.h"
#include "lw_attack.h"

/* A per-effect spec inside an attack. Mirrors leekwars/effect/
 * EffectParameters. */
typedef struct {
    int    type;             /* LW_EFFECT_* */
    double value1, value2;
    int    turns;
    int    targets_filter;   /* LW_TARGET_* bitmask (0 = no filter) */
    int    modifiers;        /* LW_MODIFIER_* bitmask */
} LwAttackEffectSpec;

/* A passive-effect spec attached to a weapon. Mirrors what Python's
 * Weapon.getPassiveEffects() returns. Each entry's ``type`` is one of
 * the passive marker effect ids (POISON_TO_SCIENCE, DAMAGE_TO_*,
 * MOVED_TO_MP, ALLY_KILLED_TO_AGILITY, KILL_TO_TP, CRITICAL_TO_HEAL).
 */
typedef struct {
    int    type;             /* LW_EFFECT_* (passive marker id) */
    double value1, value2;
    int    turns;
    int    modifiers;
} LwPassiveEffectSpec;

/* A full attack profile with multiple effects + optional weapon
 * passives that fire on damage/poison/move/etc. events. */
typedef struct {
    int  attack_type;        /* 1 = weapon, 2 = chip */
    int  item_id;
    int  min_range, max_range;
    int  launch_type;
    int  area;               /* LW_AREA_TYPE_* */
    int  needs_los;
    int  tp_cost;
    int  n_effects;
    LwAttackEffectSpec effects[8];
    int  n_passives;
    LwPassiveEffectSpec passives[4];
} LwAttackSpec;


/* ---- Passive event hooks --------------------------------------------
 *
 * Mirror Entity.onDirectDamage / onPoisonDamage / onNovaDamage /
 * onMoved / onAllyKilled / onCritical / onKill in Python. Each event
 * walks the entity's weapons (lw_catalog_get(entity.weapons[i])) and
 * for each registered passive whose type matches the event,
 * dispatches it via Effect.createEffect with the right TYPE_RAW_BUFF_*
 * target.
 *
 * Calling sites: damage/aftereffect apply paths fire on_direct_damage,
 * poison tick fires on_poison_damage, nova damage erosion fires
 * on_nova_damage, slide path fires on_moved (if mover != caster),
 * kill apply path fires on_kill (and on_ally_killed for allies of
 * the killed).
 */
void lw_event_on_direct_damage(LwState *state, int entity_idx, int value);
void lw_event_on_poison_damage(LwState *state, int entity_idx, int value);
void lw_event_on_nova_damage(LwState *state, int entity_idx, int value);
void lw_event_on_moved(LwState *state, int entity_idx, int caster_idx);
void lw_event_on_critical(LwState *state, int entity_idx);
void lw_event_on_kill(LwState *state, int killer_idx);
void lw_event_on_ally_killed(LwState *state, int dead_idx);

/*
 * Run the full attack from caster_idx on target_cell_id.
 *
 * Side effects: mutates state (HP, buff_stats, effects[], total_hp,
 * positions for movement effects), advances state->rng_n by exactly
 * 2 draws (critical + jet) regardless of what happens after.
 *
 * Returns the total damage dealt to all target entities. Heal /
 * shield / buff effects don't contribute to this total.
 */
int lw_apply_attack_full(LwState *state,
                         int caster_idx,
                         int target_cell_id,
                         const LwAttackSpec *attack);

#endif /* LW_ATTACK_APPLY_H */
