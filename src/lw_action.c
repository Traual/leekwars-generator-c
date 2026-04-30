/*
 * lw_action.c -- apply_action implementations.
 *
 * Covers END, SET_WEAPON, and MOVE in full. USE_WEAPON / USE_CHIP
 * route through the byte-for-byte attack pipeline
 * (lw_apply_attack_full) when the catalog has the item; otherwise
 * fall back to the simplified deterministic damage formula used by
 * the AI search.
 */

#include "lw_action.h"
#include "lw_attack_apply.h"
#include "lw_catalog.h"
#include <string.h>


/* -------- helpers --------------------------------------------------- */

static int weapon_index_in_inventory(const LwEntity *e, int weapon_id) {
    for (int i = 0; i < e->n_weapons; i++) {
        if (e->weapons[i] == weapon_id) return i;
    }
    return -1;
}


/* -------- per-type appliers ----------------------------------------- */

static int apply_end(LwState *s, int idx, const LwAction *a) {
    (void)s; (void)idx; (void)a;
    return 1;
}


static int apply_set_weapon(LwState *s, int idx, const LwAction *a) {
    LwEntity *e = &s->entities[idx];
    int avail_tp = (e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP]) - e->used_tp;
    if (avail_tp < 1) return 0;
    int slot = weapon_index_in_inventory(e, a->weapon_id);
    if (slot < 0) return 0;
    if (e->equipped_weapon == slot) return 0;  /* no-op switch is illegal */
    e->equipped_weapon = slot;
    e->used_tp += 1;
    return 1;
}


static int apply_move(LwState *s, int idx, const LwAction *a) {
    LwEntity *e = &s->entities[idx];
    int avail_mp = (e->base_stats[LW_STAT_MP] + e->buff_stats[LW_STAT_MP]) - e->used_mp;
    if (a->path_len <= 0) return 0;
    if (a->path_len > LW_MAX_PATH_LEN) return 0;
    if (a->path_len > avail_mp) return 0;
    if (e->cell_id < 0) return 0;

    /* Walk the path: each step releases the previous cell, claims the
     * next. The engine's moveEntity walks step-by-step so the path
     * length matches the visited count exactly. */
    int prev = e->cell_id;
    for (int i = 0; i < a->path_len; i++) {
        int next = a->path[i];
        if (next < 0 || next >= s->map.topo->n_cells) return 0;
        const LwCell *nc = &s->map.topo->cells[next];
        if (!nc->walkable) return 0;
        /* The destination of the step might be the original ``prev``
         * cell only on a no-op, which we already excluded. */
        if (s->map.entity_at_cell[next] >= 0 &&
            s->map.entity_at_cell[next] != idx) return 0;

        s->map.entity_at_cell[prev] = -1;
        s->map.entity_at_cell[next] = idx;
        prev = next;
    }
    e->cell_id = prev;
    e->used_mp += a->path_len;
    return 1;
}


/* USE_WEAPON / USE_CHIP: simplified damage / effect application.
 *
 * IMPORTANT: this is NOT Java-parity damage. It's a deterministic
 * approximation suitable for AI search scoring -- consistent across
 * clones, captures the relative quality of moves, but doesn't match
 * Java byte-for-byte. For training-data generation we still drive
 * fights through the Python engine which IS Java-parity; the C
 * engine is the AI's fast inner loop, not the fight executor.
 *
 * Damage formula:
 *   raw = (value1 + value2) / 2                          (median roll)
 *   raw *= 1 + strength/100                              (strength bonus)
 *   raw *= max(0, 1 - resistance/200)                    (resistance)
 *   absorb by target's relative_shield + absolute_shield
 *   subtract from hp (can go below 0 -> dead)
 */

static void apply_damage(LwEntity *target, double base_damage) {
    if (!target->alive) return;

    /* Resistance reduction */
    int res = target->base_stats[LW_STAT_RESISTANCE]
              + target->buff_stats[LW_STAT_RESISTANCE];
    double mult = 1.0 - (double)res / 200.0;
    if (mult < 0.0) mult = 0.0;
    double dmg = base_damage * mult;

    /* Absorb by relative shield first (percentage), then absolute */
    int rel = target->buff_stats[LW_STAT_RELATIVE_SHIELD];
    if (rel > 0) {
        double absorbed = dmg * ((double)rel / 100.0);
        if (absorbed > dmg) absorbed = dmg;
        dmg -= absorbed;
    }
    int abs_shield = target->buff_stats[LW_STAT_ABSOLUTE_SHIELD];
    if (abs_shield > 0 && dmg > 0) {
        if (abs_shield >= (int)dmg) {
            target->buff_stats[LW_STAT_ABSOLUTE_SHIELD] -= (int)dmg;
            dmg = 0;
        } else {
            dmg -= abs_shield;
            target->buff_stats[LW_STAT_ABSOLUTE_SHIELD] = 0;
        }
    }

    if (dmg > 0) {
        int dmg_int = (int)dmg;
        target->hp -= dmg_int;
        if (target->hp <= 0) {
            target->hp = 0;
            target->alive = 0;
        }
    }
}


static int apply_use_weapon_simple(LwState *s, int idx, const LwAction *a,
                                    int weapon_cost, double v1, double v2) {
    LwEntity *e = &s->entities[idx];
    int avail_tp = (e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP]) - e->used_tp;
    if (avail_tp < weapon_cost) return 0;

    int target_id = a->target_cell_id;
    if (target_id < 0 || target_id >= s->map.topo->n_cells) {
        e->used_tp += weapon_cost;
        return 1;
    }
    int target_idx = s->map.entity_at_cell[target_id];
    e->used_tp += weapon_cost;

    if (target_idx < 0 || target_idx >= s->n_entities) return 1;
    LwEntity *target = &s->entities[target_idx];

    int strength = e->base_stats[LW_STAT_STRENGTH] + e->buff_stats[LW_STAT_STRENGTH];
    double base = (v1 + v2) * 0.5 * (1.0 + (double)strength / 100.0);
    apply_damage(target, base);

    /* If the target died, clear its cell */
    if (!target->alive && target->cell_id >= 0) {
        s->map.entity_at_cell[target->cell_id] = -1;
        target->cell_id = -1;
    }
    return 1;
}


static int apply_use_chip_simple(LwState *s, int idx, const LwAction *a,
                                  int chip_cost, double v1, double v2,
                                  int effect_id) {
    LwEntity *e = &s->entities[idx];
    int avail_tp = (e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP]) - e->used_tp;
    if (avail_tp < chip_cost) return 0;
    e->used_tp += chip_cost;

    int target_id = a->target_cell_id;
    if (target_id < 0 || target_id >= s->map.topo->n_cells) return 1;
    int target_idx = s->map.entity_at_cell[target_id];
    if (target_idx < 0 || target_idx >= s->n_entities) return 1;
    LwEntity *target = &s->entities[target_idx];

    /* Dispatch by effect kind. Like apply_use_weapon, this is a
     * coarse approximation; details (turns, AoE, criticals, exact
     * shield arithmetic) are the Python engine's job. */
    int magic = e->base_stats[LW_STAT_MAGIC] + e->buff_stats[LW_STAT_MAGIC];
    double base = (v1 + v2) * 0.5 * (1.0 + (double)magic / 100.0);

    switch (effect_id) {
        case LW_EFFECT_DAMAGE:
            apply_damage(target, base);
            break;
        case LW_EFFECT_HEAL:
            target->hp += (int)base;
            if (target->hp > target->total_hp) target->hp = target->total_hp;
            break;
        case LW_EFFECT_RELATIVE_SHIELD:
            target->buff_stats[LW_STAT_RELATIVE_SHIELD] += (int)base;
            break;
        case LW_EFFECT_ABSOLUTE_SHIELD:
            target->buff_stats[LW_STAT_ABSOLUTE_SHIELD] += (int)base;
            break;
        case LW_EFFECT_BUFF_STRENGTH:
            target->buff_stats[LW_STAT_STRENGTH] += (int)base;
            break;
        case LW_EFFECT_BUFF_AGILITY:
            target->buff_stats[LW_STAT_AGILITY] += (int)base;
            break;
        case LW_EFFECT_POISON:
            /* Add a poison effect with limited turns */
            if (target->n_effects < LW_MAX_EFFECTS) {
                LwEffect *new_eff = &target->effects[target->n_effects++];
                memset(new_eff, 0, sizeof(*new_eff));
                new_eff->id = LW_EFFECT_POISON;
                new_eff->turns = 5;
                new_eff->value = (int)base;
                new_eff->value1 = v1;
                new_eff->value2 = v2;
                new_eff->target_id = target_idx;
                new_eff->caster_id = idx;
                new_eff->attack_id = a->chip_id;
            }
            break;
        default:
            break;
    }

    if (!target->alive && target->cell_id >= 0) {
        s->map.entity_at_cell[target->cell_id] = -1;
        target->cell_id = -1;
    }
    return 1;
}


/* Default cost / damage stubs when the Python catalog is absent. */
static int apply_use_weapon_stub(LwState *s, int idx, const LwAction *a) {
    return apply_use_weapon_simple(s, idx, a, 5, 80.0, 120.0);
}

static int apply_use_chip_stub(LwState *s, int idx, const LwAction *a) {
    return apply_use_chip_simple(s, idx, a, 4, 50.0, 80.0, LW_EFFECT_DAMAGE);
}


/* If the catalog has the item, run the byte-for-byte attack pipeline
 * AND charge TP. Returns 1 on success (TP charged + effects applied),
 * 0 if pipeline rejected (caller should fall back). */
static int apply_use_via_catalog(LwState *s, int idx,
                                 const LwAction *a, int item_id) {
    const LwAttackSpec *spec = lw_catalog_get(item_id);
    if (spec == NULL) return 0;

    LwEntity *e = &s->entities[idx];
    int avail_tp = (e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP]) - e->used_tp;
    if (avail_tp < spec->tp_cost) return 0;

    /* Run the byte-for-byte pipeline. It rolls critical + jet,
     * enumerates the area, and applies all effects. */
    lw_apply_attack_full(s, idx, a->target_cell_id, spec);
    e->used_tp += spec->tp_cost;
    return 1;
}


/* -------- public dispatch ------------------------------------------- */

int lw_apply_action(LwState *state,
                    int entity_index,
                    const LwAction *action) {
    if (state == NULL || action == NULL) return 0;
    if (entity_index < 0 || entity_index >= state->n_entities) return 0;
    LwEntity *e = &state->entities[entity_index];
    if (!e->alive) return 0;

    switch (action->type) {
        case LW_ACTION_END:        return apply_end(state, entity_index, action);
        case LW_ACTION_SET_WEAPON: return apply_set_weapon(state, entity_index, action);
        case LW_ACTION_MOVE:       return apply_move(state, entity_index, action);

        case LW_ACTION_USE_WEAPON:
            /* Prefer the catalog path (byte-for-byte parity); fall
             * back to the deterministic stub when the item id isn't
             * registered. The stub keeps tests + AI search working
             * even if the caller forgot to populate the catalog. */
            if (apply_use_via_catalog(state, entity_index, action,
                                       action->weapon_id)) return 1;
            return apply_use_weapon_stub(state, entity_index, action);

        case LW_ACTION_USE_CHIP:
            if (apply_use_via_catalog(state, entity_index, action,
                                       action->chip_id)) return 1;
            return apply_use_chip_stub(state, entity_index, action);

        default:                   return 0;
    }
}
