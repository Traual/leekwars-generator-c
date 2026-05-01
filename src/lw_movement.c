/*
 * lw_movement.c -- Push / Pull / Attract / Repel / Teleport / Slide.
 *
 * Mirrors the Python reference at leekwars/maps/map.py and
 * leekwars/state/state.py. The push/attract direction logic uses sign
 * triplets: (cdx, cdy) is the caster->entity direction, (dx, dy) is
 * the entity->target direction. They must agree (or be inverted for
 * attract) for the slide to happen at all.
 */

#include "lw_movement.h"


static int sgn(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }


/* True if the cell is walkable AND empty (no entity at it).
 * Mirrors Cell.available(map_) -> walkable && getEntity is None. */
static int cell_available(const LwState *state, int cid) {
    if (cid < 0) return 0;
    const LwTopology *topo = state->map.topo;
    if (cid >= topo->n_cells) return 0;
    if (!topo->cells[cid].walkable) return 0;
    return state->map.entity_at_cell[cid] < 0;
}


static int cell_id_at(const LwTopology *topo, int x, int y) {
    int gx = x - topo->min_x;
    int gy = y - topo->min_y;
    if (gx < 0 || gx >= LW_COORD_DIM) return -1;
    if (gy < 0 || gy >= LW_COORD_DIM) return -1;
    return topo->coord_lut[gx][gy];
}


int lw_compute_push_dest(const LwState *state,
                         int entity_cell, int target_cell, int caster_cell) {
    if (state == NULL) return -1;
    const LwTopology *topo = state->map.topo;
    if (entity_cell < 0 || target_cell < 0 || caster_cell < 0) return -1;
    if (entity_cell >= topo->n_cells ||
        target_cell >= topo->n_cells ||
        caster_cell >= topo->n_cells) return -1;

    const LwCell *e = &topo->cells[entity_cell];
    const LwCell *t = &topo->cells[target_cell];
    const LwCell *c = &topo->cells[caster_cell];

    int cdx = sgn(e->x - c->x);
    int cdy = sgn(e->y - c->y);
    int dx  = sgn(t->x - e->x);
    int dy  = sgn(t->y - e->y);

    /* Push only happens if entity is in the same direction from caster
     * as target is from entity (i.e. caster -> entity -> target line). */
    if (cdx != dx || cdy != dy) return entity_cell;

    int current = entity_cell;
    while (current != target_cell) {
        const LwCell *cc = &topo->cells[current];
        int next_id = cell_id_at(topo, cc->x + dx, cc->y + dy);
        if (!cell_available(state, next_id)) {
            return current;
        }
        current = next_id;
    }
    return current;
}


int lw_compute_attract_dest(const LwState *state,
                            int entity_cell, int target_cell, int caster_cell) {
    if (state == NULL) return -1;
    const LwTopology *topo = state->map.topo;
    if (entity_cell < 0 || target_cell < 0 || caster_cell < 0) return -1;

    const LwCell *e = &topo->cells[entity_cell];
    const LwCell *t = &topo->cells[target_cell];
    const LwCell *c = &topo->cells[caster_cell];

    int cdx = sgn(e->x - c->x);
    int cdy = sgn(e->y - c->y);
    int dx  = sgn(t->x - e->x);
    int dy  = sgn(t->y - e->y);

    /* Attract only happens if direction caster->entity is OPPOSITE of
     * direction entity->target (i.e. target is on caster's side). */
    if (cdx != -dx || cdy != -dy) return entity_cell;

    int current = entity_cell;
    while (current != target_cell) {
        const LwCell *cc = &topo->cells[current];
        int next_id = cell_id_at(topo, cc->x + dx, cc->y + dy);
        if (!cell_available(state, next_id)) {
            return current;
        }
        current = next_id;
    }
    return current;
}


int lw_apply_slide(LwState *state, int entity_idx, int dest_cell) {
    if (state == NULL) return 0;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return 0;
    LwEntity *e = &state->entities[entity_idx];
    if (!e->alive) return 0;
    if (e->state_flags & LW_STATE_STATIC) return 0;
    if (dest_cell < 0 || dest_cell >= state->map.topo->n_cells) return 0;

    if (e->cell_id == dest_cell) return 0;

    int from_cell = e->cell_id;
    if (e->cell_id >= 0) {
        state->map.entity_at_cell[e->cell_id] = -1;
    }
    state->map.entity_at_cell[dest_cell] = entity_idx;
    e->cell_id = dest_cell;
    lw_action_emit(state, LW_ACT_SLIDE, entity_idx, dest_cell,
                    from_cell, 0, 0);
    return 1;
}


int lw_apply_teleport(LwState *state, int entity_idx, int dest_cell) {
    if (state == NULL) return -1;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return -1;
    LwEntity *e = &state->entities[entity_idx];
    if (!e->alive) return -1;
    if (dest_cell < 0 || dest_cell >= state->map.topo->n_cells) return -1;

    if (e->cell_id == dest_cell) return 0;

    int from_cell = e->cell_id;
    if (e->cell_id >= 0) {
        state->map.entity_at_cell[e->cell_id] = -1;
    }
    state->map.entity_at_cell[dest_cell] = entity_idx;
    e->cell_id = dest_cell;
    lw_action_emit(state, LW_ACT_TELEPORT, entity_idx, dest_cell,
                    from_cell, 0, 0);
    return 1;
}


int lw_apply_permutation(LwState *state, int caster_idx, int target_idx) {
    if (state == NULL) return 0;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    if (target_idx < 0 || target_idx >= state->n_entities) return 0;
    LwEntity *c = &state->entities[caster_idx];
    LwEntity *t = &state->entities[target_idx];
    if (!c->alive || !t->alive) return 0;
    if (c->state_flags & LW_STATE_STATIC) return 0;
    if (t->state_flags & LW_STATE_STATIC) return 0;

    int c_cell = c->cell_id;
    int t_cell = t->cell_id;
    if (c_cell < 0 || t_cell < 0) return 0;
    if (c_cell == t_cell) return 0;

    state->map.entity_at_cell[c_cell] = target_idx;
    state->map.entity_at_cell[t_cell] = caster_idx;
    c->cell_id = t_cell;
    t->cell_id = c_cell;
    return 1;
}
