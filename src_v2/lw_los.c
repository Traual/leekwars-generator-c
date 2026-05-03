/*
 * lw_los.c -- 1:1 port of the line-of-sight methods of maps/Map.java.
 *
 * Methods ported (in Java source order, see Map.java lines 776-849 +
 * 1023-1031 for available() + 1164-1177 for getFirstEntity()):
 *
 *   public boolean verifyLoS(Cell start, Cell end, Attack attack);
 *   public boolean verifyLoS(Cell start, Cell end, Attack attack,
 *                             List<Cell> ignoredCells);
 *   public boolean available(Cell c, List<Cell> cells_to_ignore);
 *   public Cell    getFirstEntity(Cell from, Cell target,
 *                                  int minRange, int maxRange);
 *
 * The Bresenham-like cell line walk and the path[] integer arithmetic
 * are reproduced verbatim. The byte-for-byte parity test relies on
 * Math.ceil(y - 0.00001) and Math.floor(y + 0.00001) producing the
 * exact same integers as the JVM does -- which they will, because both
 * sides use IEEE-754 doubles.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Map.java
 */
#include "lw_los.h"

#include "lw_area.h"
#include "lw_attack.h"
#include "lw_cell.h"
#include "lw_constants.h"
#include "lw_map.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>   /* abs */


/* ---- Helpers ----------------------------------------------------- */

/* Java's `cells_to_ignore.contains(cell)` becomes a linear scan over
 * the ignore list. The list is at most 2 entries (start cell, plus the
 * optional first-entity-in-line cell), so a linear scan is fine. */
static int lw_los_ignored_contains(const int *ignored_cell_ids,
                                   int n_ignored, int cell_id) {
    if (ignored_cell_ids == NULL) return 0;
    for (int i = 0; i < n_ignored; ++i) {
        if (ignored_cell_ids[i] == cell_id) return 1;
    }
    return 0;
}


/* ---- public boolean available(Cell c, List<Cell> cells_to_ignore)
 *
 * Java:
 *   public boolean available(Cell c, List<Cell> cells_to_ignore) {
 *       if (c == null)
 *           return false;
 *       if (c.available(this))
 *           return true;
 *       if (cells_to_ignore != null && cells_to_ignore.contains(c))
 *           return true;
 *       return false;
 *   }
 */
int lw_map_los_available(const struct LwMap *self,
                         const struct LwCell *c,
                         const int *ignored_cell_ids,
                         int n_ignored) {
    if (c == NULL)
        return 0;
    if (lw_cell_available(c, self))
        return 1;
    if (ignored_cell_ids != NULL && lw_los_ignored_contains(ignored_cell_ids, n_ignored, c->id))
        return 1;
    return 0;
}


/* ---- public boolean verifyLoS(Cell start, Cell end, Attack attack,
 *                                List<Cell> ignoredCells)
 *
 * Java:
 *   public boolean verifyLoS(Cell start, Cell end, Attack attack,
 *                             List<Cell> ignoredCells) {
 *
 *       boolean needLos = attack == null ? true : attack.needLos();
 *       if (!needLos) {
 *           return true;
 *       }
 *
 *       int a = Math.abs(start.getY() - end.getY());
 *       int b = Math.abs(start.getX() - end.getX());
 *       int dx = start.getX() > end.getX() ? -1 : 1;
 *       int dy = start.getY() < end.getY() ? 1 : -1;
 *       List<Integer> path = new ArrayList<Integer>((b + 1) * 2);
 *
 *       if (b == 0) {
 *           path.add(0);
 *           path.add(a + 1);
 *       } else {
 *           double d = (double) a / (double) b / 2.0;
 *           int h = 0;
 *           for (int i = 0; i < b; ++i) {
 *               double y = 0.5 + (i * 2 + 1) * d;
 *               path.add(h);
 *               path.add((int) Math.ceil(y - 0.00001) - h);
 *               h = (int) Math.floor(y + 0.00001);
 *           }
 *           path.add(h);
 *           path.add(a + 1 - h);
 *       }
 *
 *       for (int p = 0; p < path.size(); p += 2) {
 *           for (int i = 0; i < path.get(p + 1); ++i) {
 *
 *               Cell cell = getCell(start.getX() + (p / 2) * dx, start.getY() + (path.get(p) + i) * dy);
 *
 *               if (cell == null)
 *                   return false;
 *
 *               if (needLos) {
 *                   if (!cell.isWalkable()) {
 *                       return false;
 *                   }
 *                   if (!cell.available(this)) {
 *                       // Première ou dernière cellule occupée par quelqu'un,
 *                       // c'est OK
 *                       if (cell.getId() == start.getId()) {
 *                           continue;
 *                       }
 *                       if (cell.getId() == end.getId()) {
 *                           return true;
 *                       }
 *                       if (!ignoredCells.contains(cell)) {
 *                           return false;
 *                       }
 *                   }
 *               }
 *           }
 *       }
 *       return true;
 *   }
 *
 * NOTE: `path` is a flat int list of (offset, length) pairs; we keep
 * the same layout in a stack array sized for the worst case. The Java
 * code reserves capacity (b + 1) * 2 ints (one (offset, length) pair
 * per column from x=0..b inclusive); we match that bound. The largest
 * official map is 17x17 hex variants, so b is at most ~33 in practice;
 * 128 leaves plenty of headroom.
 */
#define LW_LOS_PATH_CAP   256

int lw_map_verify_los_with_ignored(struct LwMap *self,
                                   const struct LwCell *start,
                                   const struct LwCell *end,
                                   const struct LwAttack *attack,
                                   const int *ignored_cell_ids,
                                   int n_ignored) {

    int needLos = attack == NULL ? 1 : lw_attack_need_los(attack);
    if (!needLos) {
        return 1;
    }

    int a = abs(start->y - end->y);
    int b = abs(start->x - end->x);
    int dx = start->x > end->x ? -1 : 1;
    int dy = start->y < end->y ? 1 : -1;

    /* List<Integer> path = new ArrayList<Integer>((b + 1) * 2);
     * Flat (offset, length) pair list. */
    int path[LW_LOS_PATH_CAP];
    int path_n = 0;

    if (b == 0) {
        path[path_n++] = 0;
        path[path_n++] = a + 1;
    } else {
        double d = (double) a / (double) b / 2.0;
        int h = 0;
        for (int i = 0; i < b; ++i) {
            double y = 0.5 + (i * 2 + 1) * d;
            path[path_n++] = h;
            path[path_n++] = (int) ceil(y - 0.00001) - h;
            h = (int) floor(y + 0.00001);
        }
        path[path_n++] = h;
        path[path_n++] = a + 1 - h;
    }

    for (int p = 0; p < path_n; p += 2) {
        for (int i = 0; i < path[p + 1]; ++i) {

            LwCell *cell = lw_map_get_cell_xy(self,
                                              start->x + (p / 2) * dx,
                                              start->y + (path[p] + i) * dy);

            if (cell == NULL)
                return 0;

            if (needLos) {
                if (!lw_cell_is_walkable(cell)) {
                    return 0;
                }
                if (!lw_cell_available(cell, self)) {
                    /* Première ou dernière cellule occupée par quelqu'un,
                     * c'est OK */
                    if (cell->id == start->id) {
                        continue;
                    }
                    if (cell->id == end->id) {
                        return 1;
                    }
                    if (!lw_los_ignored_contains(ignored_cell_ids, n_ignored, cell->id)) {
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}


/* ---- public boolean verifyLoS(Cell start, Cell end, Attack attack)
 *
 * Java:
 *   public boolean verifyLoS(Cell start, Cell end, Attack attack) {
 *
 *       List<Cell> ignoredCells = new ArrayList<Cell>();
 *       ignoredCells.add(start);
 *
 *       // Ignore first entity in area for Area first in line
 *       if (attack.getArea() == Area.TYPE_FIRST_IN_LINE) {
 *           Cell cell = getFirstEntity(start, end, attack.getMinRange(), attack.getMaxRange());
 *           if (cell == end) return false;
 *           if (cell != null) {
 *               ignoredCells.add(cell);
 *           }
 *       }
 *       return verifyLoS(start, end, attack, ignoredCells);
 *   }
 *
 * NOTE: The Java overload signature accepts a non-null Attack here
 * (it dereferences attack.getArea()). We mirror that strictly: when
 * attack is non-NULL, we must consult getArea(); when attack is NULL,
 * we delegate straight to the 4-arg form (which itself short-circuits
 * needLos = true and walks the line).
 */
int lw_map_verify_los(struct LwMap *self,
                      const struct LwCell *start,
                      const struct LwCell *end,
                      const struct LwAttack *attack) {

    /* List<Cell> ignoredCells = new ArrayList<Cell>(); ignoredCells.add(start); */
    int ignored_cell_ids[2];
    int n_ignored = 0;
    ignored_cell_ids[n_ignored++] = start->id;

    /* Ignore first entity in area for Area first in line */
    if (attack != NULL && lw_attack_get_area_id(attack) == LW_AREA_TYPE_FIRST_IN_LINE) {
        LwCell *cell = lw_map_get_first_entity(self,
                                               (LwCell *)start, (LwCell *)end,
                                               lw_attack_get_min_range(attack),
                                               lw_attack_get_max_range(attack));
        if (cell == end) return 0;
        if (cell != NULL) {
            ignored_cell_ids[n_ignored++] = cell->id;
        }
    }
    return lw_map_verify_los_with_ignored(self, start, end, attack,
                                          ignored_cell_ids, n_ignored);
}


/* ---- public Cell getFirstEntity(Cell from, Cell target,
 *                                  int minRange, int maxRange)
 *
 * Java:
 *   public Cell getFirstEntity(Cell from, Cell target, int minRange, int maxRange) {
 *       int dx = (int) Math.signum(target.x - from.x);
 *       int dy = (int) Math.signum(target.y - from.y);
 *       Cell current = from.next(this, dx, dy);
 *       int range = 1;
 *       while (current != null && current.isWalkable() && range <= maxRange) {
 *           if (range >= minRange && getEntity(current) != null) {
 *               return current;
 *           }
 *           current = current.next(this, dx, dy);
 *           range++;
 *       }
 *       return null;
 *   }
 *
 * NOTE: Math.signum on an int delta returns -1.0 / 0.0 / 1.0 (cast to
 * int gives -1 / 0 / 1). We expand the cast inline.
 */
static int lw_los_signum_i(int v) {
    if (v > 0) return 1;
    if (v < 0) return -1;
    return 0;
}

LwCell* lw_map_get_first_entity(const struct LwMap *self,
                                LwCell *from,
                                LwCell *target,
                                int min_range, int max_range) {
    int dx = lw_los_signum_i(target->x - from->x);
    int dy = lw_los_signum_i(target->y - from->y);
    LwCell *current = lw_cell_next(from, self, dx, dy);
    int range = 1;
    while (current != NULL && lw_cell_is_walkable(current) && range <= max_range) {
        if (range >= min_range && lw_map_get_entity(self, current) != -1) {
            return current;
        }
        current = lw_cell_next(current, self, dx, dy);
        range++;
    }
    return NULL;
}
