/*
 * lw_cell.h -- Cell struct + cell-level accessors.
 *
 * Cells are the static topology of the map. They never mutate during
 * a fight, so we keep them in a contiguous array indexed by id and
 * share that array across all clones of a State.
 */
#ifndef LW_CELL_H
#define LW_CELL_H

#include "lw_types.h"

typedef struct {
    int     id;
    int     x;
    int     y;
    uint8_t walkable;       /* 1 if walkable, 0 if obstacle/blocked */
    uint8_t obstacle_size;  /* 0 = none, else height in cells */
    uint8_t composante;     /* connected-component id (per Map) */
} LwCell;

#endif /* LW_CELL_H */
