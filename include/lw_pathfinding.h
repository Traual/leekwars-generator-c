/*
 * lw_pathfinding.h -- 1:1 port of maps/Pathfinding.java
 *
 * The Java class is a bag of public static helper methods (no instance
 * state). We port each as a free function lw_pathfinding_xxx(...).
 *
 * The direction constants (NORTH/EAST/SOUTH/WEST = 0/1/2/3) are
 * already declared in lw_constants.h as LW_DIR_NORTH/EAST/SOUTH/WEST,
 * so they are not redeclared here.
 *
 * NOTE: Pathfinding.java contains a large commented-out A*
 * implementation (`getOldAStarPath`) plus a private inner class
 * `Node` only referenced by that dead block. The Node mirror lives
 * in lw_pathfinding.c (private static struct) so the C source order
 * matches the Java source order; it is intentionally not exposed.
 *
 * Dependencies:
 *   - lw_cell.h  : LwCell struct and getX/getY
 *   - struct LwMap is forward-declared but unused by the live methods
 *     (the commented-out A* would have needed Map.getCellByDir etc.).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Pathfinding.java
 */
#ifndef LW_PATHFINDING_H
#define LW_PATHFINDING_H

#include "lw_cell.h"

/* Forward decl -- Map.java is being ported in parallel. None of the
 * live Pathfinding methods touch Map directly; the commented-out
 * A* would. Keep the forward decl here so future re-enablement is a
 * one-line change. */
struct LwMap;


/* public static boolean inLine(Cell c1, Cell c2) {
 *     return c1.getX() == c2.getX() || c1.getY() == c2.getY();
 * }
 */
int lw_pathfinding_in_line(const LwCell *c1, const LwCell *c2);


/* public static int getAverageDistance2(Cell c1, List<Cell> cells) {
 *     int dist = 0;
 *     for (Cell c2 : cells) {
 *         dist += (c1.getX() - c2.getX()) * (c1.getX() - c2.getX())
 *               + (c1.getY() - c2.getY()) * (c1.getY() - c2.getY());
 *     }
 *     return dist / cells.size();
 * }
 *
 * NOTE: Java List<Cell> -> (const LwCell **cells, int n_cells). The
 * caller owns the array of pointers. Behaviour for n_cells == 0
 * matches Java: it would throw ArithmeticException; here we return 0
 * defensively (no callers reach this with an empty list in the engine).
 */
int lw_pathfinding_get_average_distance2(const LwCell *c1,
                                          const LwCell *const *cells,
                                          int n_cells);


/* public static int getCaseDistance(Cell c1, Cell c2) {
 *     return Math.abs(c1.getX() - c2.getX()) + Math.abs(c1.getY() - c2.getY());
 * }
 */
int lw_pathfinding_get_case_distance(const LwCell *c1, const LwCell *c2);


/* public static int getCaseDistance(Cell c1, List<Cell> cells) {
 *     int dist = -1;
 *     for (Cell c2 : cells) {
 *         int d = Math.abs(c1.getX() - c2.getX()) + Math.abs(c1.getY() - c2.getY());
 *         if (dist == -1 || d < dist)
 *             dist = d;
 *     }
 *     return dist;
 * }
 *
 * NOTE: Java overload disambiguated by suffix _to_list. Returns -1 if
 * cells is empty (matches Java's initial dist=-1 with no iterations).
 */
int lw_pathfinding_get_case_distance_to_list(const LwCell *c1,
                                              const LwCell *const *cells,
                                              int n_cells);


#endif /* LW_PATHFINDING_H */
