/*
 * lw_attack_apply.c -- byte-for-byte attack execution glue.
 */

#include "lw_attack_apply.h"
#include "lw_area.h"
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
