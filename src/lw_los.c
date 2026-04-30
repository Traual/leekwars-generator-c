/*
 * lw_los.c -- Line-of-sight implementation.
 *
 * Ported from Python verifyLoS (leekwars/maps/map.py) and the Cython
 * kernel (_fast/_los.pyx). Uses the same path-walking algorithm:
 *   - At each x-step, walk a vertical "span" of cells.
 *   - The span widths are precomputed via the line-rasterisation rule
 *     used by the engine (Bresenham-like with custom rounding).
 */

#include "lw_los.h"
#include <math.h>


static int is_ignored(int cell_id, const int *ignored_ids, int n_ignored) {
    for (int i = 0; i < n_ignored; i++) {
        if (ignored_ids[i] == cell_id) return 1;
    }
    return 0;
}


int lw_verify_los(const LwMap *map,
                  int start_id,
                  int end_id,
                  const int *ignored_ids,
                  int  n_ignored,
                  int  need_los) {
    if (!need_los) return 1;
    if (map == NULL || map->topo == NULL) return 0;
    const LwTopology *topo = map->topo;
    if (start_id < 0 || start_id >= topo->n_cells) return 0;
    if (end_id   < 0 || end_id   >= topo->n_cells) return 0;

    const LwCell *s = &topo->cells[start_id];
    const LwCell *e = &topo->cells[end_id];

    int sx = s->x, sy = s->y;
    int ex = e->x, ey = e->y;

    int a = (ey > sy) ? (ey - sy) : (sy - ey);
    int b = (ex > sx) ? (ex - sx) : (sx - ex);
    int dx_step = (sx > ex) ? -1 : 1;
    int dy_step = (sy < ey) ?  1 : -1;

    /* Build the vertical-span "path" array. Pairs of (start_y_offset,
     * count), exact match to the Python rasterisation. */
    int path[64];
    int n_path = 0;
    if (b == 0) {
        path[n_path++] = 0;
        path[n_path++] = a + 1;
    } else {
        double d = (double)a / (double)b / 2.0;
        int h = 0;
        for (int i = 0; i < b; i++) {
            double y_val = 0.5 + (i * 2 + 1) * d;
            int ceil_y  = (int)ceil(y_val - 0.00001);
            int floor_y = (int)floor(y_val + 0.00001);
            path[n_path++] = h;
            path[n_path++] = ceil_y - h;
            h = floor_y;
        }
        path[n_path++] = h;
        path[n_path++] = a + 1 - h;
    }

    int n_pairs = n_path / 2;
    for (int p = 0; p < n_pairs; p++) {
        int span_start = path[2 * p];
        int span_count = path[2 * p + 1];
        int cell_x = sx + p * dx_step;
        int gx = cell_x - topo->min_x;

        for (int j = 0; j < span_count; j++) {
            int cell_y = sy + (span_start + j) * dy_step;
            int gy = cell_y - topo->min_y;

            if (gx < 0 || gx >= LW_COORD_DIM) return 0;
            if (gy < 0 || gy >= LW_COORD_DIM) return 0;
            int id = topo->coord_lut[gx][gy];
            if (id < 0) return 0;

            const LwCell *c = &topo->cells[id];
            if (!c->walkable) return 0;

            int blocker = map->entity_at_cell[id];
            if (blocker >= 0) {
                if (id == start_id) continue;
                if (id == end_id)   return 1;
                if (!is_ignored(id, ignored_ids, n_ignored)) return 0;
            }
        }
    }
    return 1;
}


int lw_verify_range(const LwMap *map,
                    int start_id,
                    int end_id,
                    const LwAttack *attack) {
    if (map == NULL || map->topo == NULL || attack == NULL) return 0;
    const LwTopology *topo = map->topo;
    if (start_id < 0 || start_id >= topo->n_cells) return 0;
    if (end_id   < 0 || end_id   >= topo->n_cells) return 0;

    const LwCell *s = &topo->cells[start_id];
    const LwCell *e = &topo->cells[end_id];

    int dx = s->x - e->x;
    int dy = s->y - e->y;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;
    int distance = adx + ady;

    if (distance > attack->max_range || distance < attack->min_range) return 0;
    if (start_id == end_id) return 1;

    int lt = attack->launch_type;
    int on_line = (dx == 0 || dy == 0);
    int on_diag = (adx == ady);
    if ((lt & 1) == 0 && on_line) return 0;
    if ((lt & 2) == 0 && on_diag) return 0;
    if ((lt & 4) == 0 && !on_line && !on_diag) return 0;
    return 1;
}


int lw_can_use_attack(const LwMap *map,
                      int start_id,
                      int end_id,
                      const LwAttack *attack) {
    if (!lw_verify_range(map, start_id, end_id, attack)) return 0;
    int ignored[1] = { start_id };
    return lw_verify_los(map, start_id, end_id, ignored, 1, attack->needs_los);
}
