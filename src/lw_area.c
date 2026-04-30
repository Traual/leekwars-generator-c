/*
 * lw_area.c -- area-of-effect cell enumeration.
 *
 * Each mask is a list of (dx, dy) offsets relative to the target
 * cell. The lists are precomputed once via lazy initialization
 * (matching Python's class-level _area attribute behavior).
 *
 * Order is significant -- it's preserved through the action stream
 * and affects which entity dies first when multiple are at threshold
 * HP under a multi-target AoE.
 */

#include "lw_area.h"
#include <stdlib.h>


/* Each mask is a list of (dx, dy). LW_MASK_MAX is a soft cap; the
 * largest shape we generate (Square 2) has 25 cells, so 64 leaves
 * plenty of headroom. */
#define LW_MASK_MAX  64

typedef struct {
    int n;
    int8_t dx[LW_MASK_MAX];
    int8_t dy[LW_MASK_MAX];
} Mask;


/* Slots indexed by area_type (1..12). Slot 0 is unused. */
static Mask masks[16];
static int  masks_built = 0;


/* generateCircleMask(min_, max_) -- byte-for-byte port. */
static void gen_circle(Mask *m, int min_v, int max_v) {
    if (min_v > max_v) { m->n = 0; return; }
    int idx = 0;
    if (min_v == 0) {
        m->dx[idx] = 0;
        m->dy[idx] = 0;
        idx++;
    }
    int start_size = (min_v < 1) ? 1 : min_v;
    for (int size = start_size; size <= max_v; size++) {
        for (int i = 0; i < size; i++) { m->dx[idx] = (int8_t)(size - i); m->dy[idx] = (int8_t)(-i);          idx++; }
        for (int i = 0; i < size; i++) { m->dx[idx] = (int8_t)(-i);      m->dy[idx] = (int8_t)(-(size - i)); idx++; }
        for (int i = 0; i < size; i++) { m->dx[idx] = (int8_t)(-(size - i)); m->dy[idx] = (int8_t)(i);       idx++; }
        for (int i = 0; i < size; i++) { m->dx[idx] = (int8_t)(i);       m->dy[idx] = (int8_t)(size - i);    idx++; }
    }
    m->n = idx;
}


static void gen_plus(Mask *m, int radius) {
    int idx = 0;
    m->dx[idx] = 0; m->dy[idx] = 0; idx++;
    for (int size = 1; size <= radius; size++) {
        m->dx[idx] = (int8_t)size;  m->dy[idx] = 0;             idx++;
        m->dx[idx] = 0;             m->dy[idx] = (int8_t)(-size); idx++;
        m->dx[idx] = (int8_t)(-size); m->dy[idx] = 0;             idx++;
        m->dx[idx] = 0;             m->dy[idx] = (int8_t)size;  idx++;
    }
    m->n = idx;
}


static void gen_x(Mask *m, int radius) {
    int idx = 0;
    m->dx[idx] = 0; m->dy[idx] = 0; idx++;
    for (int size = 1; size <= radius; size++) {
        m->dx[idx] = (int8_t)size;     m->dy[idx] = (int8_t)(-size); idx++;
        m->dx[idx] = (int8_t)(-size);  m->dy[idx] = (int8_t)(-size); idx++;
        m->dx[idx] = (int8_t)(-size);  m->dy[idx] = (int8_t)size;    idx++;
        m->dx[idx] = (int8_t)size;     m->dy[idx] = (int8_t)size;    idx++;
    }
    m->n = idx;
}


static void gen_square(Mask *m, int radius) {
    /* Inscribed circle first, then corners ring-by-ring. */
    Mask circle;
    gen_circle(&circle, 0, radius);
    int idx = 0;
    for (int i = 0; i < circle.n; i++) {
        m->dx[idx] = circle.dx[i]; m->dy[idx] = circle.dy[i]; idx++;
    }
    for (int d = 0; d < radius; d++) {
        for (int i = 1; i <= radius - d; i++) {
            m->dx[idx] = (int8_t)(radius + 1 - i); m->dy[idx] = (int8_t)(-(d + i)); idx++;
        }
        for (int i = 1; i <= radius - d; i++) {
            m->dx[idx] = (int8_t)(-(d + i));       m->dy[idx] = (int8_t)(-(radius + 1 - i)); idx++;
        }
        for (int i = 1; i <= radius - d; i++) {
            m->dx[idx] = (int8_t)(-(radius + 1 - i)); m->dy[idx] = (int8_t)(d + i); idx++;
        }
        for (int i = 1; i <= radius - d; i++) {
            m->dx[idx] = (int8_t)(d + i);          m->dy[idx] = (int8_t)(radius + 1 - i); idx++;
        }
    }
    m->n = idx;
}


/* Build all masks once on first use. Threadsafe for our use (single
 * fight-driver thread + read-only after). */
static void build_masks(void) {
    if (masks_built) return;

    /* Single cell: just (0, 0). */
    masks[LW_AREA_TYPE_SINGLE_CELL].n = 1;
    masks[LW_AREA_TYPE_SINGLE_CELL].dx[0] = 0;
    masks[LW_AREA_TYPE_SINGLE_CELL].dy[0] = 0;

    gen_circle(&masks[LW_AREA_TYPE_CIRCLE_1], 0, 1);
    gen_circle(&masks[LW_AREA_TYPE_CIRCLE_2], 0, 2);
    gen_circle(&masks[LW_AREA_TYPE_CIRCLE_3], 0, 3);

    gen_plus(&masks[LW_AREA_TYPE_PLUS_2], 2);
    gen_plus(&masks[LW_AREA_TYPE_PLUS_3], 3);

    gen_x(&masks[LW_AREA_TYPE_X_1], 1);
    gen_x(&masks[LW_AREA_TYPE_X_2], 2);
    gen_x(&masks[LW_AREA_TYPE_X_3], 3);

    gen_square(&masks[LW_AREA_TYPE_SQUARE_1], 1);
    gen_square(&masks[LW_AREA_TYPE_SQUARE_2], 2);

    masks_built = 1;
}


static int cell_id_at(const LwTopology *topo, int x, int y) {
    int gx = x - topo->min_x;
    int gy = y - topo->min_y;
    if (gx < 0 || gx >= LW_COORD_DIM) return -1;
    if (gy < 0 || gy >= LW_COORD_DIM) return -1;
    return topo->coord_lut[gx][gy];
}


int lw_area_get_mask_cells(const LwTopology *topo,
                           int area_type,
                           int target_cell_id,
                           int *out, int max_out) {
    if (topo == NULL || out == NULL || max_out <= 0) return 0;
    if (target_cell_id < 0 || target_cell_id >= topo->n_cells) return 0;
    if (area_type < 1 || area_type >= (int)(sizeof(masks)/sizeof(masks[0]))) return 0;

    build_masks();
    const Mask *m = &masks[area_type];
    if (m->n == 0) return 0;

    const LwCell *target = &topo->cells[target_cell_id];
    int written = 0;
    for (int i = 0; i < m->n && written < max_out; i++) {
        int x = target->x + m->dx[i];
        int y = target->y + m->dy[i];
        int cid = cell_id_at(topo, x, y);
        if (cid < 0) continue;
        if (!topo->cells[cid].walkable) continue;
        out[written++] = cid;
    }
    return written;
}


/* Walk one unit step from launch toward target (axis-aligned only).
 * Returns 0 dx + 0 dy if the cells aren't on the same row/col. */
static void unit_step(const LwCell *launch, const LwCell *target,
                      int *out_dx, int *out_dy) {
    *out_dx = 0;
    *out_dy = 0;
    if (launch->x == target->x) {
        *out_dy = (launch->y > target->y) ? -1 : 1;
    } else if (launch->y == target->y) {
        *out_dx = (launch->x > target->x) ? -1 : 1;
    }
}


static int laser_line_cells(const LwState *state,
                            const LwAttack *attack,
                            int launch_cell_id,
                            int target_cell_id,
                            int *out, int max_out) {
    const LwTopology *topo = state->map.topo;
    if (launch_cell_id < 0 || target_cell_id < 0) return 0;

    const LwCell *lc = &topo->cells[launch_cell_id];
    const LwCell *tc = &topo->cells[target_cell_id];
    int dx, dy;
    unit_step(lc, tc, &dx, &dy);
    if (dx == 0 && dy == 0) return 0;  /* not on same axis */

    int written = 0;
    for (int i = attack->min_range;
         i <= attack->max_range && written < max_out;
         i++) {
        int x = lc->x + dx * i;
        int y = lc->y + dy * i;
        int cid = cell_id_at(topo, x, y);
        if (cid < 0) break;
        if (attack->needs_los && !topo->cells[cid].walkable) break;
        out[written++] = cid;
    }
    return written;
}


static int first_in_line_cells(const LwState *state,
                               const LwAttack *attack,
                               int launch_cell_id,
                               int target_cell_id,
                               int *out, int max_out) {
    const LwTopology *topo = state->map.topo;
    if (launch_cell_id < 0 || target_cell_id < 0) return 0;

    const LwCell *lc = &topo->cells[launch_cell_id];
    const LwCell *tc = &topo->cells[target_cell_id];
    int dx, dy;
    unit_step(lc, tc, &dx, &dy);
    if (dx == 0 && dy == 0) return 0;

    for (int i = attack->min_range; i <= attack->max_range; i++) {
        int x = lc->x + dx * i;
        int y = lc->y + dy * i;
        int cid = cell_id_at(topo, x, y);
        if (cid < 0) break;
        int eid = state->map.entity_at_cell[cid];
        if (eid >= 0 && state->entities[eid].alive) {
            if (max_out < 1) return 0;
            out[0] = cid;
            return 1;
        }
    }
    return 0;
}


static int team_filtered_cells(const LwState *state,
                               int caster_idx,
                               int allies,  /* 1 = allies, 0 = enemies */
                               int *out, int max_out) {
    if (caster_idx < 0 || caster_idx >= state->n_entities) return 0;
    int caster_team = state->entities[caster_idx].team_id;
    int written = 0;
    for (int i = 0; i < state->n_entities && written < max_out; i++) {
        const LwEntity *e = &state->entities[i];
        if (e->cell_id < 0) continue;
        int same_team = (e->team_id == caster_team);
        if (allies != same_team) continue;
        out[written++] = e->cell_id;
    }
    return written;
}


int lw_area_get_cells(const LwState *state,
                      const LwAttack *attack,
                      int caster_idx,
                      int launch_cell_id,
                      int target_cell_id,
                      int *out, int max_out) {
    if (state == NULL || attack == NULL || out == NULL) return 0;
    int t = attack->area;
    switch (t) {
        case LW_AREA_TYPE_LASER_LINE:
            return laser_line_cells(state, attack,
                                    launch_cell_id, target_cell_id,
                                    out, max_out);
        case LW_AREA_TYPE_FIRST_IN_LINE:
            return first_in_line_cells(state, attack,
                                       launch_cell_id, target_cell_id,
                                       out, max_out);
        case LW_AREA_TYPE_ALLIES:
            return team_filtered_cells(state, caster_idx, 1, out, max_out);
        case LW_AREA_TYPE_ENEMIES:
            return team_filtered_cells(state, caster_idx, 0, out, max_out);
        default:
            return lw_area_get_mask_cells(state->map.topo, t,
                                          target_cell_id, out, max_out);
    }
}
