/*
 * lw_pathfinding_astar.c -- 1:1 port of the path-related methods of
 *                            maps/Map.java.
 *
 * The static helpers (case distance, etc.) live in lw_pathfinding.c.
 * This file holds the A* itself plus the higher-level path helpers
 * that Map.java provides:
 *
 *   public List<Cell> getAStarPath(Cell c1, List<Cell> endCells,
 *                                   List<Cell> cells_to_ignore)
 *   public List<Cell> getPathBeetween(Cell start, Cell end, ...)
 *   public List<Cell> getPathTowardLine(Cell start, Cell linecell1,
 *                                        Cell linecell2)
 *   public List<Cell> getPathAwayFromLine(Cell start, Cell linecell1,
 *                                          Cell linecell2, int max_distance)
 *   public List<Cell> getPathAway(Cell start, List<Cell> bad_cells,
 *                                  int max_distance)
 *   public List<Cell> getPathAwayMin(Map map, Cell start,
 *                                     List<Cell> bad_cells, int max_distance)
 *   public List<Cell> getPossibleCastCellsForTarget(Attack attack,
 *                                                    Cell target,
 *                                                    List<Cell> cells_to_ignore)
 *   public Cell getFirstEntity(Cell from, Cell target,
 *                               int minRange, int maxRange)
 *   public List<Cell> getValidCellsAroundObstacle(Cell cell)
 *   public boolean available(Cell c, List<Cell> cells_to_ignore)
 *   public int getDistance2(Cell c1, List<Cell> cells)
 *   public static double getDistance(Cell c1, Cell c2)
 *   public static int getDistance2(Cell c1, Cell c2)
 *
 * RNG is NOT touched here -- pure deterministic geometry/graph search.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Map.java
 */
#include "lw_map.h"
#include "lw_attack.h"
#include "lw_cell.h"
#include "lw_constants.h"
#include "lw_pathfinding.h"
#include "lw_util.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* MaskAreaCell.java mirrors -- the static methods generateMask /
 * generateCircleMask are called by Map.getPathAway and
 * Map.getPossibleCastCellsForTarget. lw_area.c has its own copies for
 * the area subclasses (with bounded LW_AREA_MASK_CAP=32 capacity); the
 * pathfinding callers want larger buffers (max_distance up to ~17), so
 * we mirror the Java source here as static helpers.
 */

/* Java: public static ArrayList<int[]> generateMask(int launchType,
 *                                                    int min, int max) {
 *     if (min > max)
 *         return new ArrayList<>();
 *
 *     var cells = new ArrayList<int[]>();
 *     int len = (launchType == 9 || launchType == 10) ? max
 *               : ((launchType & 1) != 0 ? max
 *                  : ((launchType & 4) != 0 ? max - 1 : max / 2));
 *     for (int i = 0; i < len * 2 + 1; ++i) {
 *         for (int j = 0; j < len * 2 + 1; ++j) {
 *             int x = i - len;
 *             int y = j - len;
 *             boolean in_range = Math.abs(x) + Math.abs(y) <= max
 *                                && Math.abs(x) + Math.abs(y) >= min;
 *             boolean condition = (((launchType & 1) != 0) && (x == 0 || y == 0))
 *                 || (((launchType & 2) != 0) && Math.abs(x) == Math.abs(y))
 *                 || (((launchType & 4) != 0) && ((x == 0 && y == 0)
 *                     || (Math.abs(x) != Math.abs(y) && x != 0 && y != 0)));
 *             if (in_range && condition) {
 *                 cells.add(new int[] { x, y });
 *             }
 *         }
 *     }
 *     return cells;
 * }
 */
int lw_mask_area_cell_generate_mask(int launch_type, int min, int max,
                                            int (*out)[2], int out_cap) {
    if (min > max)
        return 0;
    int n = 0;
    int len = (launch_type == 9 || launch_type == 10) ? max
              : ((launch_type & 1) != 0 ? max
                 : ((launch_type & 4) != 0 ? max - 1 : max / 2));
    for (int i = 0; i < len * 2 + 1; ++i) {
        for (int j = 0; j < len * 2 + 1; ++j) {
            int x = i - len;
            int y = j - len;
            int in_range = (abs(x) + abs(y) <= max) && (abs(x) + abs(y) >= min);
            int condition = (((launch_type & 1) != 0) && (x == 0 || y == 0))
                || (((launch_type & 2) != 0) && abs(x) == abs(y))
                || (((launch_type & 4) != 0) && ((x == 0 && y == 0)
                    || (abs(x) != abs(y) && x != 0 && y != 0)));
            if (in_range && condition) {
                if (n < out_cap) {
                    out[n][0] = x;
                    out[n][1] = y;
                }
                n++;
            }
        }
    }
    return n;
}


/* Java: public static int[][] generateCircleMask(int min, int max) {
 *     if (min > max)
 *         return null;
 *     int nbCells = 2 * (min + max) * (max - min + 1);
 *     if (min == 0) nbCells += 1;
 *     int[][] retour = new int[nbCells][2];
 *     int index = 0;
 *     if (min == 0) {
 *         retour[index++] = new int[] { 0, 0 };
 *     }
 *     for (int size = (min < 1 ? 1 : min); size <= max; size++) {
 *         for (int i = 0; i < size; i++) retour[index++] = new int[] {  size - i, -i };
 *         for (int i = 0; i < size; i++) retour[index++] = new int[] { -i, -(size - i) };
 *         for (int i = 0; i < size; i++) retour[index++] = new int[] { -(size - i), i };
 *         for (int i = 0; i < size; i++) retour[index++] = new int[] {  i, size - i };
 *     }
 *     return retour;
 * }
 *
 * NOTE: returns -1 to signal "min > max" (Java's null), 0 means an
 * empty result.
 */
int lw_mask_area_cell_generate_circle_mask(int min, int max,
                                                   int (*out)[2], int out_cap) {
    if (min > max)
        return -1;
    int index = 0;
    if (min == 0) {
        if (index < out_cap) {
            out[index][0] = 0;
            out[index][1] = 0;
        }
        index++;
    }
    for (int size = (min < 1 ? 1 : min); size <= max; size++) {
        for (int i = 0; i < size; i++) {
            if (index < out_cap) { out[index][0] =  size - i; out[index][1] = -i; }
            index++;
        }
        for (int i = 0; i < size; i++) {
            if (index < out_cap) { out[index][0] = -i; out[index][1] = -(size - i); }
            index++;
        }
        for (int i = 0; i < size; i++) {
            if (index < out_cap) { out[index][0] = -(size - i); out[index][1] = i; }
            index++;
        }
        for (int i = 0; i < size; i++) {
            if (index < out_cap) { out[index][0] = i; out[index][1] = size - i; }
            index++;
        }
    }
    return index;
}


/* ============================================================== */
/*  Static distance helpers (Java: public static)                  */
/* ============================================================== */

/* Java: public static int getDistance2(Cell c1, Cell c2) {
 *     return (c1.getX() - c2.getX()) * (c1.getX() - c2.getX())
 *          + (c1.getY() - c2.getY()) * (c1.getY() - c2.getY());
 * }
 */
int lw_map_get_distance2(const LwCell *c1, const LwCell *c2) {
    return (lw_cell_get_x(c1) - lw_cell_get_x(c2)) * (lw_cell_get_x(c1) - lw_cell_get_x(c2))
         + (lw_cell_get_y(c1) - lw_cell_get_y(c2)) * (lw_cell_get_y(c1) - lw_cell_get_y(c2));
}


/* Java: public static double getDistance(Cell c1, Cell c2) {
 *     return Math.sqrt(getDistance2(c1, c2));
 * }
 */
double lw_map_get_distance(const LwCell *c1, const LwCell *c2) {
    return sqrt((double) lw_map_get_distance2(c1, c2));
}


/* Java: public int getDistance2(Cell c1, List<Cell> cells) {
 *     int dist = -1;
 *     for (Cell c2 : cells) {
 *         int d = (c1.getX() - c2.getX()) * (c1.getX() - c2.getX())
 *               + (c1.getY() - c2.getY()) * (c1.getY() - c2.getY());
 *         if (dist == -1 || d < dist)
 *             dist = d;
 *     }
 *     return dist;
 * }
 */
int lw_map_get_distance2_to_list(const LwCell *c1,
                                 LwCell *const *cells, int n_cells) {
    int dist = -1;
    for (int i = 0; i < n_cells; i++) {
        const LwCell *c2 = cells[i];
        int d = (lw_cell_get_x(c1) - lw_cell_get_x(c2)) * (lw_cell_get_x(c1) - lw_cell_get_x(c2))
              + (lw_cell_get_y(c1) - lw_cell_get_y(c2)) * (lw_cell_get_y(c1) - lw_cell_get_y(c2));
        if (dist == -1 || d < dist)
            dist = d;
    }
    return dist;
}


/* ============================================================== */
/*  available() with cells_to_ignore                                */
/* ============================================================== */

/* Java: public boolean available(Cell c, List<Cell> cells_to_ignore) {
 *     if (c == null)
 *         return false;
 *     if (c.available(this))
 *         return true;
 *     if (cells_to_ignore != null && cells_to_ignore.contains(c))
 *         return true;
 *     return false;
 * }
 */
int lw_map_available_with_ignore(LwMap *self, LwCell *c,
                                 LwCell *const *cells_to_ignore,
                                 int n_cells_to_ignore) {
    if (c == NULL)
        return 0;
    if (lw_cell_available(c, self))
        return 1;
    if (cells_to_ignore != NULL) {
        for (int i = 0; i < n_cells_to_ignore; i++) {
            if (cells_to_ignore[i] == c) return 1;
        }
    }
    return 0;
}


/* Helper: List<Cell>.contains via pointer equality (matches Java). */
static int list_contains(LwCell *const *list, int n_list, const LwCell *c) {
    if (list == NULL) return 0;
    for (int i = 0; i < n_list; i++) {
        if (list[i] == c) return 1;
    }
    return 0;
}


/* ============================================================== */
/*  A* open set -- TreeSet<Cell> mirror                             */
/* ============================================================== */

/* Java:
 *   TreeSet<Cell> open = new TreeSet<>(new Comparator<Cell>() {
 *       @Override
 *       public int compare(Cell o1, Cell o2) {
 *           return o1.weight > o2.weight ? 1 : -1;
 *       }
 *   });
 *
 * The comparator NEVER returns 0, so equal-weight nodes are never
 * deduplicated by the TreeSet. compare(new, existing) returns -1 when
 * weights are equal (because new.weight > existing.weight is false), so
 * the new node always goes to the LEFT subtree, i.e. ahead of the
 * existing node in iteration order. pollFirst() therefore returns the
 * MOST recently added node when weights tie (LIFO for ties).
 *
 * We mirror this with a sorted array: when weights are equal, the new
 * entry is inserted BEFORE existing entries with the same weight (lower
 * index = popped first).
 */
#define LW_ASTAR_OPEN_CAP   LW_MAP_MAX_CELLS

static int astar_open_n;
static LwCell *astar_open[LW_ASTAR_OPEN_CAP];

static void astar_open_clear(void) {
    astar_open_n = 0;
}

/* Insert `c` so that the array stays sorted ascending by `weight`,
 * placing new ties at the front of equal-weight runs (LIFO for ties --
 * see the TreeSet comment above). Linear walk from the back. */
static void astar_open_add(LwCell *c) {
    if (astar_open_n >= LW_ASTAR_OPEN_CAP) return;
    int i = astar_open_n;
    /* Walk back while existing.weight >= c.weight (so c lands in front
     * of the equal-weight run, matching Java's "new goes left" tie
     * behaviour). */
    while (i > 0 && astar_open[i - 1]->weight >= c->weight) {
        astar_open[i] = astar_open[i - 1];
        --i;
    }
    astar_open[i] = c;
    astar_open_n++;
}

static LwCell* astar_open_poll_first(void) {
    if (astar_open_n == 0) return NULL;
    LwCell *c = astar_open[0];
    /* Shift left by one. */
    for (int i = 1; i < astar_open_n; i++) {
        astar_open[i - 1] = astar_open[i];
    }
    astar_open_n--;
    return c;
}


/* ============================================================== */
/*  getAStarPath                                                     */
/* ============================================================== */

/* Java: public List<Cell> getAStarPath(Cell c1, List<Cell> endCells,
 *                                       List<Cell> cells_to_ignore)
 *
 * (Plus the two thin overloads that pass a Cell[] or default null
 *  cells_to_ignore -- those are handled at the call site in C.)
 *
 *  if (c1 == null || endCells == null || endCells.isEmpty())
 *      return null;
 *  if (endCells.contains(c1))
 *      return null;
 *
 *  for (Cell c : getCells()) {
 *      c.visited = false;
 *      c.closed = false;
 *      c.cost = Short.MAX_VALUE;
 *  }
 *
 *  TreeSet<Cell> open = new TreeSet<>(new Comparator<Cell>() {
 *      @Override
 *      public int compare(Cell o1, Cell o2) {
 *          return o1.weight > o2.weight ? 1 : -1;
 *      }
 *  });
 *  c1.cost = 0;
 *  c1.weight = 0;
 *  c1.visited = true;
 *  open.add(c1);
 *
 *  while (open.size() > 0) {
 *      Cell u = open.pollFirst();
 *      u.closed = true;
 *
 *      if (endCells.contains(u)) {
 *          List<Cell> result = new ArrayList<>();
 *          int s = u.cost;
 *          while (s-- >= 1) {
 *              result.add(u);
 *              u = u.parent;
 *          }
 *          Collections.reverse(result);
 *          Cell last = result.get(result.size() - 1);
 *          if (last.getPlayer(this) != null && (cells_to_ignore == null
 *                                                || !cells_to_ignore.contains(last))) {
 *              result.remove(result.size() - 1);
 *          }
 *          return result;
 *      }
 *
 *      for (Cell c : getCellsAround(u)) {
 *          if (c == null || c.closed || !c.isWalkable()) continue;
 *          if (c.getPlayer(this) != null && (cells_to_ignore == null
 *                                             || !cells_to_ignore.contains(c))
 *                                       && !endCells.contains(c)) continue;
 *
 *          if (!c.visited || u.cost + 1 < c.cost) {
 *              c.cost = (short) (u.cost + 1);
 *              c.weight = c.cost + (float) getDistance(c, endCells.get(0));
 *              c.parent = u;
 *              if (!c.visited) {
 *                  open.add(c);
 *                  c.visited = true;
 *              }
 *          }
 *      }
 *  }
 *  // System.out.println("No path found!");
 *  return null;
 */
int lw_map_get_a_star_path(LwMap *self,
                           LwCell *from,
                           LwCell **targets, int n_targets,
                           LwCell **forbidden, int n_forbidden,
                           LwCell **out_buf, int out_cap) {
    /* Java: c1 == null || endCells == null || endCells.isEmpty() */
    if (from == NULL || targets == NULL || n_targets <= 0)
        return -1;
    /* Java: endCells.contains(c1) */
    if (list_contains(targets, n_targets, from))
        return -1;

    /* Reset scratch fields on every cell. */
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *c = &self->cells[i];
        c->visited = 0;
        c->closed = 0;
        c->cost = INT16_MAX;  /* Java: Short.MAX_VALUE */
    }

    astar_open_clear();
    from->cost = 0;
    from->weight = 0.0f;
    from->visited = 1;
    astar_open_add(from);

    while (astar_open_n > 0) {
        LwCell *u = astar_open_poll_first();
        u->closed = 1;

        if (list_contains(targets, n_targets, u)) {
            /* Java: List<Cell> result = new ArrayList<>();
             *       int s = u.cost;
             *       while (s-- >= 1) { result.add(u); u = u.parent; }
             *       Collections.reverse(result);
             *
             * Reverse-fill out_buf so that out_buf[0] is the cell next
             * to `from` (matches Java's reversed list). */
            int s = (int)u->cost;
            int n = s;  /* result will end up `s` long */
            if (n > out_cap) n = out_cap;
            int idx = n - 1;
            int written = 0;
            while (s-- >= 1) {
                if (idx >= 0) {
                    out_buf[idx] = u;
                    idx--;
                    written++;
                }
                u = u->parent;
            }
            int result_size = written;
            /* Java:
             *   Cell last = result.get(result.size() - 1);
             *   if (last.getPlayer(this) != null && (cells_to_ignore == null
             *                                  || !cells_to_ignore.contains(last))) {
             *       result.remove(result.size() - 1);
             *   }
             */
            if (result_size > 0) {
                LwCell *last = out_buf[result_size - 1];
                if (lw_cell_get_player(last, self) != NULL
                    && (forbidden == NULL || !list_contains(forbidden, n_forbidden, last))) {
                    result_size--;
                }
            }
            return result_size;
        }

        LwCell *around[4];
        lw_map_get_cells_around(self, u, around);
        for (int i = 0; i < 4; i++) {
            LwCell *c = around[i];
            if (c == NULL || c->closed || !lw_cell_is_walkable(c)) continue;
            if (lw_cell_get_player(c, self) != NULL
                && (forbidden == NULL || !list_contains(forbidden, n_forbidden, c))
                && !list_contains(targets, n_targets, c)) continue;

            if (!c->visited || u->cost + 1 < c->cost) {
                c->cost = (short) (u->cost + 1);
                c->weight = (float) c->cost + (float) lw_map_get_distance(c, targets[0]);
                c->parent = u;
                if (!c->visited) {
                    astar_open_add(c);
                    c->visited = 1;
                }
            }
        }
    }
    /* System.out.println("No path found!"); */
    return -1;
}


/* ============================================================== */
/*  getPathBeetween                                                  */
/* ============================================================== */

/* Java: public List<Cell> getPathBeetween(Cell start, Cell end,
 *                                          List<Cell> cells_to_ignore) {
 *     if (start == null || end == null)
 *         return null;
 *     ...
 *     List<Cell> r = getAStarPath(start, new Cell[] { end }, cells_to_ignore);
 *     return r;
 * }
 *
 * The cache code is commented out in Java; we don't carry a cache.
 *
 * NOTE: the v2 engine's call site (lw_state.c) doesn't pass a
 * cells_to_ignore list, so we omit it from the C signature. The
 * underlying A* still receives NULL/0 for forbidden, matching Java's
 * default-overload semantics.
 */
int lw_map_get_path_between(LwMap *self,
                            LwCell *start, LwCell *end,
                            LwCell **out_buf, int out_cap) {
    if (start == NULL || end == NULL)
        return -1;
    LwCell *targets[1]; targets[0] = end;
    return lw_map_get_a_star_path(self, start, targets, 1, NULL, 0, out_buf, out_cap);
}


/* ============================================================== */
/*  getValidCellsAroundObstacle + addValidCell                      */
/* ============================================================== */

/* Java: private boolean addValidCell(List<Cell> retour, List<Cell> close,
 *                                     Cell c, Cell center) {
 *     if (c == null) {
 *         return true;
 *     }
 *     int dx = (int) Math.signum(center.getX() - c.getX());
 *     int dy = (int) Math.signum(center.getY() - c.getY());
 *
 *     Cell c1 = getCell(c.getX() + dx, c.getY());
 *     Cell c2 = getCell(c.getX(), c.getY() + dy);
 *
 *     if (!c.isWalkable()) {
 *         if ((c1 != null && !c1.isWalkable() && close.contains(c1))
 *             || (c2 != null && !c2.isWalkable() && close.contains(c2))) {
 *             close.add(c);
 *             return false;
 *         }
 *     } else {
 *         if ((c1 != null && !c1.isWalkable() && close.contains(c1))
 *             || (c2 != null && !c2.isWalkable() && close.contains(c2))) {
 *             retour.add(c);
 *         }
 *     }
 *     return true;
 * }
 */
static int add_valid_cell(LwMap *self,
                          LwCell **retour, int *n_retour, int retour_cap,
                          LwCell **close,  int *n_close,  int close_cap,
                          LwCell *c, LwCell *center) {
    if (c == NULL) {
        return 1;
    }
    int dx = lw_java_signum_int(center->x - c->x);
    int dy = lw_java_signum_int(center->y - c->y);

    LwCell *c1 = lw_map_get_cell_xy(self, c->x + dx, c->y);
    LwCell *c2 = lw_map_get_cell_xy(self, c->x, c->y + dy);

    if (!lw_cell_is_walkable(c)) {
        if ((c1 != NULL && !lw_cell_is_walkable(c1) && list_contains(close, *n_close, c1))
            || (c2 != NULL && !lw_cell_is_walkable(c2) && list_contains(close, *n_close, c2))) {
            if (*n_close < close_cap) close[(*n_close)++] = c;
            return 0;
        }
    } else {
        if ((c1 != NULL && !lw_cell_is_walkable(c1) && list_contains(close, *n_close, c1))
            || (c2 != NULL && !lw_cell_is_walkable(c2) && list_contains(close, *n_close, c2))) {
            if (*n_retour < retour_cap) retour[(*n_retour)++] = c;
        }
    }
    return 1;
}


/* Java: public List<Cell> getValidCellsAroundObstacle(Cell cell) {
 *     List<Cell> retour = new ArrayList<Cell>();
 *     int size = 1;
 *     List<Cell> close = new ArrayList<Cell>();
 *     close.add(cell);
 *
 *     for (int i = 1; i <= size; i++) {
 *         boolean stop = true;
 *         for (int j = 0; j < i; j++) {
 *             stop = addValidCell(retour, close, getCell(cell.getX() + j, cell.getY() + (i - j)), cell) && stop;
 *             stop = addValidCell(retour, close, getCell(cell.getX() - j, cell.getY() - (i - j)), cell) && stop;
 *             stop = addValidCell(retour, close, getCell(cell.getX() + i - j, cell.getY() - j), cell) && stop;
 *             stop = addValidCell(retour, close, getCell(cell.getX() - i + j, cell.getY() + j), cell) && stop;
 *         }
 *         if (!stop && size < 5)
 *             size++;
 *     }
 *     return retour;
 * }
 */
int lw_map_get_valid_cells_around_obstacle(LwMap *self,
                                           LwCell *cell,
                                           LwCell **out_buf, int out_cap) {
    int n_retour = 0;
    static LwCell *close[LW_MAP_MAX_CELLS];
    int n_close = 0;
    close[n_close++] = cell;

    int size = 1;
    for (int i = 1; i <= size; i++) {
        int stop = 1;
        for (int j = 0; j < i; j++) {
            int s;
            s = add_valid_cell(self, out_buf, &n_retour, out_cap,
                                close, &n_close, LW_MAP_MAX_CELLS,
                                lw_map_get_cell_xy(self, cell->x + j, cell->y + (i - j)), cell);
            stop = s && stop;
            s = add_valid_cell(self, out_buf, &n_retour, out_cap,
                                close, &n_close, LW_MAP_MAX_CELLS,
                                lw_map_get_cell_xy(self, cell->x - j, cell->y - (i - j)), cell);
            stop = s && stop;
            s = add_valid_cell(self, out_buf, &n_retour, out_cap,
                                close, &n_close, LW_MAP_MAX_CELLS,
                                lw_map_get_cell_xy(self, cell->x + i - j, cell->y - j), cell);
            stop = s && stop;
            s = add_valid_cell(self, out_buf, &n_retour, out_cap,
                                close, &n_close, LW_MAP_MAX_CELLS,
                                lw_map_get_cell_xy(self, cell->x - i + j, cell->y + j), cell);
            stop = s && stop;
        }
        if (!stop && size < 5)
            size++;
    }
    return n_retour;
}


/* ============================================================== */
/*  getFirstEntity                                                   */
/* ============================================================== */

/* Java: public Cell getFirstEntity(Cell from, Cell target,
 *                                   int minRange, int maxRange) {
 *     int dx = (int) Math.signum(target.x - from.x);
 *     int dy = (int) Math.signum(target.y - from.y);
 *     Cell current = from.next(this, dx, dy);
 *     int range = 1;
 *     while (current != null && current.isWalkable() && range <= maxRange) {
 *         if (range >= minRange && getEntity(current) != null) {
 *             return current;
 *         }
 *         current = current.next(this, dx, dy);
 *         range++;
 *     }
 *     return null;
 * }
 */
/* lw_map_get_first_entity moved to lw_los.c (also a 1:1 port). Kept
 * the comment block above for traceability. */


/* ============================================================== */
/*  getPathTowardLine / getPathAwayFromLine / getPathAway           */
/* ============================================================== */

/* Java: public List<Cell> getPathTowardLine(Cell start, Cell linecell1,
 *                                            Cell linecell2) {
 *     // On crée la liste des cellules à rejoindre
 *     List<Cell> line_cell = new ArrayList<Cell>();
 *     // On trouve la ligne
 *     int dx = (int) Math.signum(linecell2.getX() - linecell1.getX());
 *     int dy = (int) Math.signum(linecell2.getY() - linecell1.getY());
 *     if (dx == 0 && dy == 0)
 *         return null;
 *     // On prolonge la ligne
 *     Cell curent = linecell1;
 *     while (curent != null) {
 *         line_cell.add(curent);
 *         curent = getCell(curent.getX() + dx, curent.getY() + dy);
 *     }
 *     curent = getCell(linecell1.getX() - dx, linecell1.getY() - dy);
 *     while (curent != null) {
 *         line_cell.add(curent);
 *         curent = getCell(curent.getX() - dx, curent.getY() - dy);
 *     }
 *     // Puis on crée un path qui va vers de ces cellules
 *     return getAStarPath(start, line_cell);
 * }
 */
int lw_map_get_path_toward_line(LwMap *self,
                                LwCell *start,
                                LwCell *linecell1,
                                LwCell *linecell2,
                                LwCell **out_buf, int out_cap) {
    /* On crée la liste des cellules à rejoindre */
    static LwCell *line_cell[LW_MAP_MAX_CELLS];
    int n_line_cell = 0;
    /* On trouve la ligne */
    int dx = lw_java_signum_int(linecell2->x - linecell1->x);
    int dy = lw_java_signum_int(linecell2->y - linecell1->y);
    if (dx == 0 && dy == 0)
        return -1;
    /* On prolonge la ligne */
    LwCell *curent = linecell1;
    while (curent != NULL) {
        if (n_line_cell < LW_MAP_MAX_CELLS) line_cell[n_line_cell++] = curent;
        curent = lw_map_get_cell_xy(self, curent->x + dx, curent->y + dy);
    }
    curent = lw_map_get_cell_xy(self, linecell1->x - dx, linecell1->y - dy);
    while (curent != NULL) {
        if (n_line_cell < LW_MAP_MAX_CELLS) line_cell[n_line_cell++] = curent;
        curent = lw_map_get_cell_xy(self, curent->x - dx, curent->y - dy);
    }
    /* Puis on crée un path qui va vers de ces cellules */
    return lw_map_get_a_star_path(self, start, line_cell, n_line_cell,
                                   NULL, 0, out_buf, out_cap);
}


/* Java: public List<Cell> getPathAway(Cell start, List<Cell> bad_cells,
 *                                      int max_distance) {
 *     if (start == null)
 *         return null;
 *     int curent_distance = getDistance2(start, bad_cells);
 *     List<CellDistance> potential_targets = new ArrayList<CellDistance>();
 *     int[][] cells = MaskAreaCell.generateCircleMask(1, max_distance);
 *     if (cells == null)
 *         return null;
 *     int x = start.getX(), y = start.getY();
 *     for (int i = 0; i < cells.length; i++) {
 *         Cell c = getCell(x + cells[i][0], y + cells[i][1]);
 *         if (c == null || !c.available(this))
 *             continue;
 *         int distance = getDistance2(c, bad_cells);
 *         if (distance > curent_distance)
 *             potential_targets.add(new CellDistance(c, distance));
 *     }
 *     if (potential_targets.size() == 0)
 *         return null;
 *     Collections.sort(potential_targets, new CellDistanceComparator());
 *     List<Cell> path = null;
 *     for (CellDistance c : potential_targets) {
 *         // Calcule des path
 *         path = getAStarPath(start, new Cell[] { c.getCell() });
 *         if (path != null && path.size() <= max_distance)
 *             break;
 *         else
 *             path = null;
 *     }
 *     return path;
 * }
 *
 * NOTE: CellDistanceComparator sorts by descending distance:
 *   if (cell1.getDistance() > cell2.getDistance()) return -1;
 *   else if (cell1.getDistance() == cell2.getDistance()) return 0;
 *   return 1;
 *
 * Java's Collections.sort is STABLE, so equal-distance entries keep
 * their insertion order. We mirror with a stable insertion sort.
 */
typedef struct {
    LwCell *cell;
    int     distance;
} LwCellDistance;

/* Stable sort by descending distance. */
static void cell_distance_sort_desc_stable(LwCellDistance *arr, int n) {
    /* Insertion sort: stable, O(n^2) but n is bounded by mask size (~25
     * for max max_distance). */
    for (int i = 1; i < n; i++) {
        LwCellDistance key = arr[i];
        int j = i - 1;
        /* Move while strictly less than key (desc), keeping stability. */
        while (j >= 0 && arr[j].distance < key.distance) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

int lw_map_get_path_away(LwMap *self,
                         LwCell *start,
                         LwCell **bad_cells, int n_bad_cells,
                         int max_distance,
                         LwCell **out_buf, int out_cap) {
    if (start == NULL)
        return -1;
    int curent_distance = lw_map_get_distance2_to_list(start, bad_cells, n_bad_cells);
    static LwCellDistance potential_targets[LW_MAP_MAX_CELLS];
    int n_potential = 0;
    /* MaskAreaCell.generateCircleMask(1, max_distance) -- size is
     *   2 * (1 + max_distance) * (max_distance - 1 + 1) = 2*(1+max)*max
     * for min=1; bounded by LW_MAP_MAX_CELLS for sane max_distance. */
    static int cells[LW_MAP_MAX_CELLS][2];
    int n_cells = lw_mask_area_cell_generate_circle_mask(1, max_distance,
                                                          cells, LW_MAP_MAX_CELLS);
    if (n_cells <= 0)
        return -1;
    int x = lw_cell_get_x(start), y = lw_cell_get_y(start);
    for (int i = 0; i < n_cells; i++) {
        LwCell *c = lw_map_get_cell_xy(self, x + cells[i][0], y + cells[i][1]);
        if (c == NULL || !lw_cell_available(c, self))
            continue;
        int distance = lw_map_get_distance2_to_list(c, bad_cells, n_bad_cells);
        if (distance > curent_distance) {
            if (n_potential < LW_MAP_MAX_CELLS) {
                potential_targets[n_potential].cell = c;
                potential_targets[n_potential].distance = distance;
                n_potential++;
            }
        }
    }
    if (n_potential == 0)
        return -1;
    cell_distance_sort_desc_stable(potential_targets, n_potential);
    int path_size = -1;
    for (int i = 0; i < n_potential; i++) {
        /* path = getAStarPath(start, new Cell[] { c.getCell() }); */
        LwCell *targets[1]; targets[0] = potential_targets[i].cell;
        path_size = lw_map_get_a_star_path(self, start, targets, 1,
                                           NULL, 0, out_buf, out_cap);
        if (path_size >= 0 && path_size <= max_distance)
            break;
        else
            path_size = -1;
    }
    return path_size;
}


/* Java: public List<Cell> getPathAwayFromLine(Cell start, Cell linecell1,
 *                                              Cell linecell2, int max_distance) {
 *     if (start == null) {
 *         return null;
 *     }
 *     // On crée la liste des cellules à fuir
 *     List<Cell> line_cell = new ArrayList<Cell>();
 *     int dx = (int) Math.signum(linecell2.getX() - linecell1.getX());
 *     int dy = (int) Math.signum(linecell2.getY() - linecell1.getY());
 *     if (dx == 0 && dy == 0)
 *         return null;
 *     // On prolonge la ligne
 *     Cell current = linecell1;
 *     while (current != null) {
 *         line_cell.add(current);
 *         current = getCell(current.getX() + dx, current.getY() + dy);
 *     }
 *     current = getCell(linecell1.getX() - dx, linecell1.getY() - dy);
 *     while (current != null) {
 *         line_cell.add(current);
 *         current = getCell(current.getX() - dx, current.getY() - dy);
 *     }
 *     // Puis on crée un path qui va loin de ces cellules
 *     List<Cell> cells = getPathAway(start, line_cell, max_distance);
 *     return cells;
 * }
 */
int lw_map_get_path_away_from_line(LwMap *self,
                                   LwCell *start,
                                   LwCell *linecell1,
                                   LwCell *linecell2,
                                   int max_distance,
                                   LwCell **out_buf, int out_cap) {
    if (start == NULL) {
        return -1;
    }
    /* On crée la liste des cellules à fuir */
    static LwCell *line_cell[LW_MAP_MAX_CELLS];
    int n_line_cell = 0;
    int dx = lw_java_signum_int(linecell2->x - linecell1->x);
    int dy = lw_java_signum_int(linecell2->y - linecell1->y);
    if (dx == 0 && dy == 0)
        return -1;
    /* On prolonge la ligne */
    LwCell *current = linecell1;
    while (current != NULL) {
        if (n_line_cell < LW_MAP_MAX_CELLS) line_cell[n_line_cell++] = current;
        current = lw_map_get_cell_xy(self, current->x + dx, current->y + dy);
    }
    current = lw_map_get_cell_xy(self, linecell1->x - dx, linecell1->y - dy);
    while (current != NULL) {
        if (n_line_cell < LW_MAP_MAX_CELLS) line_cell[n_line_cell++] = current;
        current = lw_map_get_cell_xy(self, current->x - dx, current->y - dy);
    }
    /* Puis on crée un path qui va loin de ces cellules */
    return lw_map_get_path_away(self, start, line_cell, n_line_cell,
                                max_distance, out_buf, out_cap);
}


/* Java: public List<Cell> getPathAwayMin(Map map, Cell start,
 *                                         List<Cell> bad_cells, int max_distance) {
 *     int curent_distance = getDistance2(start, bad_cells);
 *     List<CellDistance> potential_targets = new ArrayList<CellDistance>();
 *     int[][] cells = MaskAreaCell.generateCircleMask(1, max_distance);
 *     int x = start.getX(), y = start.getY();
 *     for (int i = 0; i < cells.length; i++) {
 *         Cell c = map.getCell(x + cells[i][0], y + cells[i][1]);
 *         if (c == null || !c.available(map))
 *             continue;
 *         int distance = getDistance2(c, bad_cells);
 *         if (distance > curent_distance)
 *             potential_targets.add(new CellDistance(c, distance));
 *     }
 *     if (potential_targets.size() == 0)
 *         return null;
 *     Collections.sort(potential_targets, new CellDistanceComparator());
 *     List<Cell> path = null;
 *     for (CellDistance c : potential_targets) {
 *         path = getAStarPath(start, new Cell[] { c.getCell() });
 *         if (path != null && path.size() <= max_distance)
 *             break;
 *         else
 *             path = null;
 *     }
 *     return path;
 * }
 *
 * NOTE: identical logic to getPathAway, but the Cell.available() check
 * is performed against `map` (which is passed in) rather than `this`.
 * In practice the Java callers always pass `map == this`, but we
 * preserve the parameter for parity.
 */
int lw_map_get_path_away_min(LwMap *self,
                             LwMap *map,
                             LwCell *start,
                             LwCell **bad_cells, int n_bad_cells,
                             int max_distance,
                             LwCell **out_buf, int out_cap) {
    int curent_distance = lw_map_get_distance2_to_list(start, bad_cells, n_bad_cells);
    static LwCellDistance potential_targets[LW_MAP_MAX_CELLS];
    int n_potential = 0;
    static int cells[LW_MAP_MAX_CELLS][2];
    int n_cells = lw_mask_area_cell_generate_circle_mask(1, max_distance,
                                                          cells, LW_MAP_MAX_CELLS);
    if (n_cells <= 0)
        return -1;
    int x = lw_cell_get_x(start), y = lw_cell_get_y(start);
    for (int i = 0; i < n_cells; i++) {
        LwCell *c = lw_map_get_cell_xy(map, x + cells[i][0], y + cells[i][1]);
        if (c == NULL || !lw_cell_available(c, map))
            continue;
        int distance = lw_map_get_distance2_to_list(c, bad_cells, n_bad_cells);
        if (distance > curent_distance) {
            if (n_potential < LW_MAP_MAX_CELLS) {
                potential_targets[n_potential].cell = c;
                potential_targets[n_potential].distance = distance;
                n_potential++;
            }
        }
    }
    if (n_potential == 0)
        return -1;
    cell_distance_sort_desc_stable(potential_targets, n_potential);
    int path_size = -1;
    for (int i = 0; i < n_potential; i++) {
        LwCell *targets[1]; targets[0] = potential_targets[i].cell;
        path_size = lw_map_get_a_star_path(self, start, targets, 1,
                                           NULL, 0, out_buf, out_cap);
        if (path_size >= 0 && path_size <= max_distance)
            break;
        else
            path_size = -1;
    }
    return path_size;
}


/* ============================================================== */
/*  getPossibleCastCellsForTarget                                    */
/* ============================================================== */

/* Java: public List<Cell> getPossibleCastCellsForTarget(Attack attack,
 *                                                        Cell target,
 *                                                        List<Cell> cells_to_ignore) {
 *     if (target == null) {
 *         return null;
 *     }
 *     List<Cell> possible = new ArrayList<Cell>();
 *
 *     if (target.isWalkable()) {
 *
 *         if (attack.getLaunchType() == Attack.LAUNCH_TYPE_LINE) {
 *             var line = new boolean[] { true, true, true, true };
 *             int x = target.getX(), y = target.getY();
 *             var dirs = new int[][] { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
 *             Cell c;
 *             for (int i = 0; i <= attack.getMaxRange(); i++) {
 *                 for (int dir = 0; dir < 4; dir++) {
 *                     if (!line[dir])
 *                         continue;
 *                     c = getCell(x + i * dirs[dir][0], y + i * dirs[dir][1]);
 *                     if (c == null)
 *                         line[dir] = false;
 *                     else {
 *                         if (attack.needLos() && !available(c, cells_to_ignore) && i > 0)
 *                             line[dir] = false;
 *                         else if (attack.needLos() && !c.isWalkable())
 *                             line[dir] = false;
 *                         else if (attack.getMinRange() <= i && available(c, cells_to_ignore))
 *                             possible.add(c);
 *                     }
 *                 }
 *             }
 *         } else {
 *             var mask = MaskAreaCell.generateMask(attack.getLaunchType(),
 *                                                   attack.getMinRange(),
 *                                                   attack.getMaxRange());
 *             int x = target.getX();
 *             int y = target.getY();
 *             Cell cell;
 *             for (var mask_cell : mask) {
 *                 cell = getCell(x + mask_cell[0], y + mask_cell[1]);
 *                 if (cell == null || !available(cell, cells_to_ignore))
 *                     continue;
 *                 if (!verifyLoS(cell, target, attack, cells_to_ignore))
 *                     continue;
 *                 possible.add(cell);
 *             }
 *         }
 *     }
 *     return possible;
 * }
 *
 * NOTE: the verifyLoS overload that takes ignoredCells doesn't yet
 * exist in the C port. lw_map_verify_los() takes (map, start, end,
 * attack) without an ignore list. Until the LoS port lands we use the
 * existing 4-arg form -- which currently always returns 1 (LoS-free
 * stub). Document the gap so the caller knows.
 */
int lw_map_get_possible_cast_cells_for_target(LwMap *self,
                                              const struct LwAttack *attack,
                                              LwCell *target,
                                              LwCell **cells_to_ignore,
                                              int n_cells_to_ignore,
                                              LwCell **out_buf, int out_cap) {
    if (target == NULL) {
        return -1;
    }
    int n = 0;

    if (lw_cell_is_walkable(target)) {

        if (lw_attack_get_launch_type(attack) == LW_ATTACK_LAUNCH_TYPE_LINE) {
            int line[4] = { 1, 1, 1, 1 };
            int x = lw_cell_get_x(target), y = lw_cell_get_y(target);
            int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            LwCell *c;
            int max_range = lw_attack_get_max_range(attack);
            int min_range = lw_attack_get_min_range(attack);
            int need_los  = lw_attack_need_los(attack);
            for (int i = 0; i <= max_range; i++) {
                for (int dir = 0; dir < 4; dir++) {
                    if (!line[dir])
                        continue;
                    c = lw_map_get_cell_xy(self, x + i * dirs[dir][0], y + i * dirs[dir][1]);
                    if (c == NULL) {
                        line[dir] = 0;
                    } else {
                        if (need_los && !lw_map_available_with_ignore(self, c, cells_to_ignore, n_cells_to_ignore) && i > 0)
                            line[dir] = 0;
                        else if (need_los && !lw_cell_is_walkable(c))
                            line[dir] = 0;
                        else if (min_range <= i && lw_map_available_with_ignore(self, c, cells_to_ignore, n_cells_to_ignore)) {
                            if (n < out_cap) out_buf[n] = c;
                            n++;
                        }
                    }
                }
            }
        } else {
            static int mask[LW_MAP_MAX_CELLS][2];
            int n_mask = lw_mask_area_cell_generate_mask(lw_attack_get_launch_type(attack),
                                                          lw_attack_get_min_range(attack),
                                                          lw_attack_get_max_range(attack),
                                                          mask, LW_MAP_MAX_CELLS);
            int x = lw_cell_get_x(target);
            int y = lw_cell_get_y(target);
            LwCell *cell;
            for (int mi = 0; mi < n_mask; mi++) {
                cell = lw_map_get_cell_xy(self, x + mask[mi][0], y + mask[mi][1]);
                if (cell == NULL || !lw_map_available_with_ignore(self, cell, cells_to_ignore, n_cells_to_ignore))
                    continue;
                /* Java: verifyLoS(cell, target, attack, cells_to_ignore).
                 * The C port currently uses the no-ignore form -- when
                 * the full LoS port lands, switch to the variant. */
                if (!lw_map_verify_los(self, cell, target, attack))
                    continue;
                if (n < out_cap) out_buf[n] = cell;
                n++;
            }
        }
    }
    return n;
}
