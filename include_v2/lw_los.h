/*
 * lw_los.h -- 1:1 port of the line-of-sight methods of maps/Map.java.
 *
 * The Java source has all map methods on the single `Map` class. We
 * split the LoS-related ones into their own translation unit so the
 * verifyLoS / getFirstEntity walk-the-line logic can be audited in
 * isolation. Public API names stay `lw_map_*` to match the existing
 * forward declarations in lw_map.h.
 *
 * Methods ported (each is a static helper inside lw_los.c, except for
 * the two public entry points exposed below):
 *
 *   public boolean verifyLoS(Cell start, Cell end, Attack attack);
 *   public boolean verifyLoS(Cell start, Cell end, Attack attack,
 *                             List<Cell> ignoredCells);
 *   public boolean available(Cell c, List<Cell> cells_to_ignore);
 *   public Cell    getFirstEntity(Cell from, Cell target,
 *                                  int minRange, int maxRange);
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Map.java
 */
#ifndef LW_LOS_H
#define LW_LOS_H

#include "lw_cell.h"

/* Forward decls -- the structs are defined in their own headers. */
struct LwMap;
struct LwAttack;


/* public boolean verifyLoS(Cell start, Cell end, Attack attack)
 *
 * Public entry point. Builds the implicit ignore list (containing just
 * `start`, plus the first-entity-in-line cell when the attack's area
 * is FIRST_IN_LINE) and forwards to the 4-arg overload.
 *
 * The signature is also forward-declared in lw_map.h so callers don't
 * need to include this header. */
int  lw_map_verify_los(struct LwMap *self,
                       const struct LwCell *start,
                       const struct LwCell *end,
                       const struct LwAttack *attack);


/* public boolean verifyLoS(Cell start, Cell end, Attack attack,
 *                           List<Cell> ignoredCells)
 *
 * Walks the cell line from start to end and returns 0 (false) the first
 * time it hits an obstacle or an entity that isn't in `ignoredCells`.
 *
 * `ignored_cell_ids` is a contiguous array of cell ids to ignore (Java
 * uses `List<Cell>.contains`). Pass NULL/0 when there is nothing to
 * ignore. */
int  lw_map_verify_los_with_ignored(struct LwMap *self,
                                    const struct LwCell *start,
                                    const struct LwCell *end,
                                    const struct LwAttack *attack,
                                    const int *ignored_cell_ids,
                                    int n_ignored);


/* public boolean available(Cell c, List<Cell> cells_to_ignore)
 *
 * "Cell is empty, OR is in the ignore list." Mirrors Map.available --
 * not Cell.available -- which is the helper used inside verifyLoS /
 * getPossibleCastCellsForTarget.
 *
 * Pass ignored_cell_ids == NULL / n_ignored == 0 to skip the ignore
 * check (Java: cells_to_ignore == null). */
int  lw_map_los_available(const struct LwMap *self,
                          const struct LwCell *c,
                          const int *ignored_cell_ids,
                          int n_ignored);


/* public Cell getFirstEntity(Cell from, Cell target, int minRange, int maxRange)
 *
 * Marches one cell at a time from `from` toward `target` in the
 * Math.signum direction and returns the first walkable cell that holds
 * an entity. Returns NULL if none is found within [minRange, maxRange].
 *
 * Used by AreaFirstInLine.getArea() and by verifyLoS() (4-arg overload
 * builder) when the attack's area is TYPE_FIRST_IN_LINE. */
struct LwCell* lw_map_get_first_entity(const struct LwMap *self,
                                       struct LwCell *from,
                                       struct LwCell *target,
                                       int min_range, int max_range);


#endif /* LW_LOS_H */
