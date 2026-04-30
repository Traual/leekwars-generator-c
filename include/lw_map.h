/*
 * lw_map.h -- Map struct.
 *
 * The Map's cells/coord_lut/neighbors arrays are STATIC topology and
 * are shared between the original State and all its clones (we copy
 * the pointer, not the data). entity_at_cell DOES mutate per-state,
 * so it's stored per-Map and copied on clone.
 */
#ifndef LW_MAP_H
#define LW_MAP_H

#include "lw_types.h"
#include "lw_cell.h"

/* Topology -- shared across State clones. Built once per fight. */
typedef struct {
    int      id;
    int      width;
    int      height;
    int      n_cells;
    int      min_x, max_x, min_y, max_y;

    LwCell   cells[LW_MAX_CELLS];

    /* coord_lut[gx][gy] = cell.id, or -1 if no cell at that position.
     * gx = x - min_x, gy = y - min_y. */
    int      coord_lut[LW_COORD_DIM][LW_COORD_DIM];

    /* neighbors[id] = (south, west, north, east) cell ids; -1 if none. */
    int      neighbors[LW_MAX_CELLS][4];
} LwTopology;

typedef struct {
    /* Pointer to shared topology (never owns it). */
    const LwTopology *topo;

    /* Mutable: which entity occupies which cell. -1 = empty. */
    int entity_at_cell[LW_MAX_CELLS];
} LwMap;

#endif /* LW_MAP_H */
