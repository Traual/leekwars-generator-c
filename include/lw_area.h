/*
 * lw_area.h -- area-of-effect cell enumeration.
 *
 * Given an attack's area type and a target cell, return the list of
 * affected cell ids. The order matters: it's preserved as the order
 * in which entities take damage from a single AoE attack (and that's
 * observable in the action log + can change which entity dies first
 * when multiple are at threshold HP).
 *
 * Two flavors:
 *
 *   - lw_area_get_mask_cells: pure-mask shapes (Circle/Plus/X/Square
 *     1..3 + Single Cell). Just topology lookup + walkable filter.
 *
 *   - lw_area_get_cells: full path that also handles the dynamic shapes
 *     (LaserLine, FirstInLine, Allies, Enemies) which need range/LoS/
 *     entity info.
 *
 * Both return the count of cells written (capped at max_out).
 */
#ifndef LW_AREA_H
#define LW_AREA_H

#include "lw_attack.h"
#include "lw_state.h"

/* Area types -- mirror leekwars/area/area.py::Area.TYPE_*. */
#define LW_AREA_TYPE_SINGLE_CELL      1
#define LW_AREA_TYPE_LASER_LINE       2
#define LW_AREA_TYPE_CIRCLE_1         3   /* == AREA_PLUS_1 in Python */
#define LW_AREA_TYPE_CIRCLE_2         4
#define LW_AREA_TYPE_CIRCLE_3         5
#define LW_AREA_TYPE_PLUS_2           6
#define LW_AREA_TYPE_PLUS_3           7
#define LW_AREA_TYPE_X_1              8
#define LW_AREA_TYPE_X_2              9
#define LW_AREA_TYPE_X_3             10
#define LW_AREA_TYPE_SQUARE_1        11
#define LW_AREA_TYPE_SQUARE_2        12
#define LW_AREA_TYPE_FIRST_IN_LINE   13
#define LW_AREA_TYPE_ENEMIES         14
#define LW_AREA_TYPE_ALLIES          15

/*
 * Pure mask shapes. Returns number of cell ids written (<= max_out).
 *
 * area_type must be one of:
 *   SINGLE_CELL / CIRCLE_1..3 / PLUS_2..3 / X_1..3 / SQUARE_1..2.
 *
 * The order matches the Python generators (generateCircleMask,
 * generatePlusMask, generateXMask, generateSquareMask) byte-for-byte.
 *
 * Cells outside the map or non-walkable are filtered out -- the
 * remaining cells keep their relative order.
 */
int lw_area_get_mask_cells(const LwTopology *topo,
                           int area_type,
                           int target_cell_id,
                           int *out, int max_out);

/*
 * Full area enumeration (handles dynamic shapes too). For
 * LaserLine, walks from launch toward target in unit steps in
 * [min_range, max_range], stopping at first non-walkable if
 * needs_los is set. For FirstInLine, returns the first ALIVE entity's
 * cell encountered along the same walk (or empty). For Allies /
 * Enemies, ignores launch/target and just enumerates entities by team.
 *
 * Mask-based area types delegate to lw_area_get_mask_cells, so
 * callers can use a single entry point.
 */
int lw_area_get_cells(const LwState *state,
                      const LwAttack *attack,
                      int caster_idx,
                      int launch_cell_id,
                      int target_cell_id,
                      int *out, int max_out);

#endif /* LW_AREA_H */
