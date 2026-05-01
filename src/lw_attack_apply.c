/*
 * lw_attack_apply.c -- byte-for-byte attack execution glue + passive
 * event hooks (onDirectDamage / onPoisonDamage / onNovaDamage /
 * onMoved / onAllyKilled / onCritical / onKill).
 */

#include "lw_attack_apply.h"
#include "lw_area.h"
#include "lw_catalog.h"
#include "lw_critical.h"
#include "lw_effect_dispatch.h"
#include "lw_effect.h"
#include "lw_movement.h"
#include "lw_rng.h"
#include <stdlib.h>


/* Manhattan distance between two cells. Mirrors
 * Pathfinding.getCaseDistance(c1, c2). */
static int case_distance(const LwTopology *topo, int c1, int c2) {
    if (c1 < 0 || c2 < 0) return 0;
    int dx = topo->cells[c1].x - topo->cells[c2].x;
    int dy = topo->cells[c1].y - topo->cells[c2].y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx + dy;
}


/* Power scaling for an entity's cell relative to the attack's epicenter.
 * Mirrors Attack.getPowerForCell:
 *   1.0 for line / first-in-line / allies / enemies
 *   1 - dist * 0.2 otherwise. */
static double power_for_cell(const LwTopology *topo,
                             int area_type,
                             int target_cell, int current_cell) {
    if (area_type == LW_AREA_TYPE_LASER_LINE ||
        area_type == LW_AREA_TYPE_FIRST_IN_LINE ||
        area_type == LW_AREA_TYPE_ALLIES ||
        area_type == LW_AREA_TYPE_ENEMIES) {
        return 1.0;
    }
    int d = case_distance(topo, target_cell, current_cell);
    return 1.0 - (double)d * 0.2;
}


/* Filter a target by the effect's targets_filter bitmask + caster.
 * Returns 1 to keep, 0 to drop. Mirrors Attack.filterTarget. */
static int filter_target(int targets, int caster_idx, int target_idx,
                         const LwState *state) {
    if (targets == 0) return 1;  /* unspecified -> accept all */
    const LwEntity *caster = &state->entities[caster_idx];
    const LwEntity *target = &state->entities[target_idx];

    /* Enemies */
    if ((targets & LW_TARGET_ENEMIES) == 0 &&
        caster->team_id != target->team_id) return 0;
    /* Allies */
    if ((targets & LW_TARGET_ALLIES) == 0 &&
        caster->team_id == target->team_id && caster_idx != target_idx) return 0;
    /* Caster */
    if ((targets & LW_TARGET_CASTER) == 0 && caster_idx == target_idx) return 0;
    /* Summons / non-summons not modeled yet -- caller's spec should
     * not depend on those bits. */
    return 1;
}


int lw_apply_attack_full(LwState *state,
                         int caster_idx,
                         int target_cell_id,
                         const LwAttackSpec *attack) {
    if (state == NULL || attack == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_cell_id < 0 ||
        target_cell_id >= state->map.topo->n_cells) return 0;

    LwEntity *caster = &state->entities[caster_idx];
    if (!caster->alive) return 0;

    /* Build a temporary LwAttack profile so the area module can use
     * its existing API. */
    LwAttack a;
    a.min_range = attack->min_range;
    a.max_range = attack->max_range;
    a.launch_type = attack->launch_type;
    a.area = attack->area;
    a.needs_los = attack->needs_los;
    a.value1 = 0; a.value2 = 0;
    a.n_effects = 0;
    a.effect_id = 0; a.effect_v1 = 0; a.effect_v2 = 0; a.effect_turns = 0;

    /* 1. Roll critical (consumes 1 RNG draw). */
    int critical = lw_roll_critical(state, caster_idx);

    /* 2. Roll jet (consumes 1 RNG draw). */
    double jet = lw_rng_double(&state->rng_n);

    /* 3. Enumerate target cells. */
    int target_cells[LW_MAX_CELLS];
    int n_cells = lw_area_get_cells(state, &a, caster_idx,
                                     caster->cell_id, target_cell_id,
                                     target_cells, LW_MAX_CELLS);
    if (n_cells <= 0) return 0;

    /* 4. Find target entities at each cell (preserves order). */
    int target_entities[LW_MAX_ENTITIES];
    int target_cell_for_entity[LW_MAX_ENTITIES];  /* cell that hosted this entity */
    int n_targets = 0;
    for (int i = 0; i < n_cells; i++) {
        int eid = state->map.entity_at_cell[target_cells[i]];
        if (eid < 0 || eid >= state->n_entities) continue;
        if (!state->entities[eid].alive) continue;
        target_entities[n_targets] = eid;
        target_cell_for_entity[n_targets] = target_cells[i];
        n_targets++;
    }

    /* 5. For each effect, apply to each filtered target. */
    int total_damage = 0;
    int previous_value = 0;
    const LwTopology *topo = state->map.topo;

    for (int ef = 0; ef < attack->n_effects; ef++) {
        const LwAttackEffectSpec *spec = &attack->effects[ef];
        int t = spec->type;

        /* Movement-by-effect short-circuits (matches the explicit
         * branch in attack.py). */
        if (t == LW_EFFECT_ATTRACT || t == LW_EFFECT_PUSH || t == LW_EFFECT_REPEL) {
            for (int i = 0; i < n_targets; i++) {
                int tgt_idx = target_entities[i];
                if (!filter_target(spec->targets_filter, caster_idx, tgt_idx, state)) continue;
                int dest = (t == LW_EFFECT_ATTRACT)
                         ? lw_compute_attract_dest(state,
                              state->entities[tgt_idx].cell_id,
                              target_cell_id, caster->cell_id)
                         : lw_compute_push_dest(state,
                              state->entities[tgt_idx].cell_id,
                              target_cell_id, caster->cell_id);
                lw_apply_slide(state, tgt_idx, dest);
            }
            continue;
        }
        if (t == LW_EFFECT_TELEPORT) {
            /* Caster moves to target_cell_id (Python: state.teleportEntity(caster, target, ...)). */
            lw_apply_teleport(state, caster_idx, target_cell_id);
            continue;
        }

        /* MULTIPLIED_BY_TARGETS: count the entities that passed filter. */
        int effective_target_count = 1;
        if (spec->modifiers & LW_MODIFIER_MULTIPLIED_BY_TARGETS) {
            int n_filtered = 0;
            for (int i = 0; i < n_targets; i++) {
                if (filter_target(spec->targets_filter, caster_idx,
                                   target_entities[i], state)) n_filtered++;
            }
            effective_target_count = n_filtered;
            if (effective_target_count < 1) effective_target_count = 1;
        }

        int effect_total = 0;
        for (int i = 0; i < n_targets; i++) {
            int tgt_idx = target_entities[i];
            if (!filter_target(spec->targets_filter, caster_idx, tgt_idx, state)) continue;

            double aoe = power_for_cell(topo, attack->area, target_cell_id,
                                         target_cell_for_entity[i]);

            LwEffectInput p = {0};
            p.type = spec->type;
            p.caster_idx = caster_idx;
            p.target_idx = tgt_idx;
            p.value1 = spec->value1;
            p.value2 = spec->value2;
            p.jet = jet;
            p.turns = spec->turns;
            p.aoe = aoe;
            p.critical = critical;
            p.attack_id = attack->item_id;
            p.modifiers = spec->modifiers;
            p.previous_value = previous_value;
            p.target_count = effective_target_count;

            int v = lw_effect_create(state, &p);
            effect_total += v;

            /* Track damage-like contributions in the running total. */
            switch (spec->type) {
                case LW_EFFECT_DAMAGE:
                case LW_EFFECT_AFTEREFFECT:
                case LW_EFFECT_NOVA_DAMAGE:
                case LW_EFFECT_LIFE_DAMAGE:
                case LW_EFFECT_KILL:
                    total_damage += v;
                    break;
                default: break;
            }
        }
        previous_value = effect_total;
    }

    /* TP cost is the caller's responsibility (the action handler in
     * lw_action.c will subtract). */

    return total_damage;
}


/* ---- Passive event hooks ---------------------------------------- */

/* Walk an entity's weapons, look up each via the catalog, and call
 * ``visit`` on every passive whose type is in the matching set. The
 * visitor decides what to dispatch (each event has its own logic
 * because the resulting createEffect call differs in target type). */
typedef void (*passive_visit_fn)(LwState *state, int entity_idx,
                                  const LwPassiveEffectSpec *p,
                                  int input_value, int weapon_item_id);

static void walk_passives(LwState *state, int entity_idx,
                           passive_visit_fn visit, int input_value) {
    if (state == NULL) return;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return;
    const LwEntity *e = &state->entities[entity_idx];
    if (!e->alive) return;
    for (int wi = 0; wi < e->n_weapons; wi++) {
        int item_id = e->weapons[wi];
        const LwAttackSpec *spec = lw_catalog_get(item_id);
        if (spec == NULL) continue;
        for (int pi = 0; pi < spec->n_passives; pi++) {
            visit(state, entity_idx, &spec->passives[pi], input_value, item_id);
        }
    }
}


/* Helper: dispatch a passive into Effect.createEffect on the given
 * target (always self for the on-X events) with the right TYPE_*. */
static void fire_passive_buff(LwState *state, int entity_idx,
                              const LwPassiveEffectSpec *p,
                              int target_buff_type,
                              double v1, double v2,
                              int item_id) {
    LwEffectInput in = {0};
    in.type = target_buff_type;
    in.caster_idx = entity_idx;
    in.target_idx = entity_idx;
    in.value1 = v1;
    in.value2 = v2;
    in.jet = 0.0;
    in.turns = p->turns;
    in.aoe = 1.0;
    in.critical = 0;
    in.attack_id = item_id;
    in.modifiers = p->modifiers;
    in.previous_value = 0;
    in.target_count = 1;
    lw_effect_create(state, &in);
}


/* on_direct_damage: walks passives on the DAMAGED entity's weapons
 * (they react to incoming damage). For DAMAGE_TO_ABSOLUTE_SHIELD
 * fires RAW_ABSOLUTE_SHIELD; for DAMAGE_TO_STRENGTH fires
 * RAW_BUFF_STRENGTH. */
static void visit_on_direct_damage(LwState *state, int entity_idx,
                                    const LwPassiveEffectSpec *p,
                                    int input_value, int item_id) {
    if (p->type == LW_EFFECT_DAMAGE_TO_ABSOLUTE_SHIELD) {
        double v = (double)input_value * (p->value1 / 100.0);
        fire_passive_buff(state, entity_idx, p,
                           LW_EFFECT_RAW_ABSOLUTE_SHIELD, v, 0, item_id);
    } else if (p->type == LW_EFFECT_DAMAGE_TO_STRENGTH) {
        double v = (double)input_value * (p->value1 / 100.0);
        fire_passive_buff(state, entity_idx, p,
                           LW_EFFECT_RAW_BUFF_STRENGTH, v, 0, item_id);
    }
}

void lw_event_on_direct_damage(LwState *state, int entity_idx, int value) {
    if (value <= 0) return;
    walk_passives(state, entity_idx, visit_on_direct_damage, value);
}


/* on_poison_damage: POISON_TO_SCIENCE -> RAW_BUFF_SCIENCE. */
static void visit_on_poison_damage(LwState *state, int entity_idx,
                                    const LwPassiveEffectSpec *p,
                                    int input_value, int item_id) {
    if (p->type == LW_EFFECT_POISON_TO_SCIENCE) {
        double v = (double)input_value * (p->value1 / 100.0);
        fire_passive_buff(state, entity_idx, p,
                           LW_EFFECT_RAW_BUFF_SCIENCE, v, 0, item_id);
    }
}

void lw_event_on_poison_damage(LwState *state, int entity_idx, int value) {
    if (value <= 0) return;
    walk_passives(state, entity_idx, visit_on_poison_damage, value);
}


/* on_nova_damage: NOVA_DAMAGE_TO_MAGIC -> RAW_BUFF_MAGIC. */
static void visit_on_nova_damage(LwState *state, int entity_idx,
                                  const LwPassiveEffectSpec *p,
                                  int input_value, int item_id) {
    if (p->type == LW_EFFECT_NOVA_DAMAGE_TO_MAGIC) {
        double v = (double)input_value * (p->value1 / 100.0);
        fire_passive_buff(state, entity_idx, p,
                           LW_EFFECT_RAW_BUFF_MAGIC, v, 0, item_id);
    }
}

void lw_event_on_nova_damage(LwState *state, int entity_idx, int value) {
    if (value <= 0) return;
    walk_passives(state, entity_idx, visit_on_nova_damage, value);
}


/* on_moved: MOVED_TO_MP -> RAW_BUFF_MP. Only fires if mover != caster. */
static void visit_on_moved(LwState *state, int entity_idx,
                            const LwPassiveEffectSpec *p,
                            int input_value, int item_id) {
    (void)input_value;
    if (p->type == LW_EFFECT_MOVED_TO_MP) {
        fire_passive_buff(state, entity_idx, p,
                           LW_EFFECT_RAW_BUFF_MP, p->value1, 0, item_id);
    }
}

void lw_event_on_moved(LwState *state, int entity_idx, int caster_idx) {
    if (caster_idx == entity_idx) return;  /* Python skips self-moves. */
    walk_passives(state, entity_idx, visit_on_moved, 0);
}


/* on_critical: CRITICAL_TO_HEAL -> RAW_HEAL (turns=0). */
static void visit_on_critical(LwState *state, int entity_idx,
                               const LwPassiveEffectSpec *p,
                               int input_value, int item_id) {
    (void)input_value;
    if (p->type == LW_EFFECT_CRITICAL_TO_HEAL) {
        /* Python skips if at full life. */
        const LwEntity *e = &state->entities[entity_idx];
        if (e->hp >= e->total_hp) return;
        /* Python rolls a fresh jet for the heal. We inherit the
         * caller's RNG state. */
        double jet = lw_rng_double(&state->rng_n);
        LwEffectInput in = {0};
        in.type = LW_EFFECT_RAW_HEAL;
        in.caster_idx = entity_idx;
        in.target_idx = entity_idx;
        in.value1 = p->value1;
        in.value2 = p->value2;
        in.jet = jet;
        in.turns = 0;
        in.aoe = 1.0;
        in.critical = 0;
        in.attack_id = item_id;
        in.modifiers = p->modifiers;
        in.target_count = 1;
        lw_effect_create(state, &in);
    }
}

void lw_event_on_critical(LwState *state, int entity_idx) {
    walk_passives(state, entity_idx, visit_on_critical, 0);
}


/* on_kill: KILL_TO_TP -> RAW_BUFF_TP. */
static void visit_on_kill(LwState *state, int entity_idx,
                           const LwPassiveEffectSpec *p,
                           int input_value, int item_id) {
    (void)input_value;
    if (p->type == LW_EFFECT_KILL_TO_TP) {
        /* Python uses `value1, value1` for v1+v2 (matches its source). */
        fire_passive_buff(state, entity_idx, p,
                           LW_EFFECT_RAW_BUFF_TP, p->value1, p->value1,
                           item_id);
    }
}

void lw_event_on_kill(LwState *state, int killer_idx) {
    walk_passives(state, killer_idx, visit_on_kill, 0);
}


/* on_ally_killed: ALLY_KILLED_TO_AGILITY -> RAW_BUFF_AGILITY for each
 * ALIVE ally of the dead entity. */
static void visit_on_ally_killed(LwState *state, int entity_idx,
                                  const LwPassiveEffectSpec *p,
                                  int input_value, int item_id) {
    (void)input_value;
    if (p->type == LW_EFFECT_ALLY_KILLED_TO_AGILITY) {
        fire_passive_buff(state, entity_idx, p,
                           LW_EFFECT_RAW_BUFF_AGILITY, p->value1, 0,
                           item_id);
    }
}

void lw_event_on_ally_killed(LwState *state, int dead_idx) {
    if (state == NULL) return;
    if (dead_idx < 0 || dead_idx >= state->n_entities) return;
    int dead_team = state->entities[dead_idx].team_id;
    for (int i = 0; i < state->n_entities; i++) {
        if (i == dead_idx) continue;
        const LwEntity *e = &state->entities[i];
        if (!e->alive) continue;
        if (e->team_id != dead_team) continue;
        walk_passives(state, i, visit_on_ally_killed, 0);
    }
}
