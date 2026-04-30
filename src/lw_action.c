/*
 * lw_action.c -- apply_action implementations.
 *
 * Covers END, SET_WEAPON, and MOVE in full. USE_WEAPON / USE_CHIP
 * are stubs that charge TP only -- the damage/effects logic ships in
 * a follow-up commit.
 */

#include "lw_action.h"
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


/* USE_WEAPON / USE_CHIP stubs: charge TP and (eventually) trigger
 * the damage/effects machinery. For now they just deduct TP and
 * return success; a follow-up commit ships the full simulation. */
static int apply_use_weapon_stub(LwState *s, int idx, const LwAction *a) {
    (void)a;
    LwEntity *e = &s->entities[idx];
    /* We don't yet have a weapon catalog in C; assume cost is in
     * range [3, 9] and cap at available TP. The real apply_action
     * will pull cost from the equipped weapon. */
    int default_cost = 5;
    int avail_tp = (e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP]) - e->used_tp;
    if (avail_tp < default_cost) return 0;
    e->used_tp += default_cost;
    return 1;
}


static int apply_use_chip_stub(LwState *s, int idx, const LwAction *a) {
    (void)a;
    LwEntity *e = &s->entities[idx];
    int default_cost = 4;
    int avail_tp = (e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP]) - e->used_tp;
    if (avail_tp < default_cost) return 0;
    e->used_tp += default_cost;
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
        case LW_ACTION_USE_WEAPON: return apply_use_weapon_stub(state, entity_index, action);
        case LW_ACTION_USE_CHIP:   return apply_use_chip_stub(state, entity_index, action);
        default:                   return 0;
    }
}
