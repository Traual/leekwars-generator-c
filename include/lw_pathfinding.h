/*
 * lw_pathfinding.h -- A* and BFS-bounded reachability.
 *
 * The Python engine's A* (leekwars/maps/map.py:getAStarPath) uses
 * LIFO tie-breaking to match Java's TreeSet-with-always-negative
 * comparator behaviour. We replicate that here with a decreasing
 * counter as the secondary heap key.
 *
 * BFS-bounded is what AI move enumeration uses: from a start cell,
 * find every walkable+unoccupied cell reachable in <= max_dist
 * steps, with the path back to start. Replaces ~600 A* calls per
 * AI turn with one BFS.
 */
#ifndef LW_PATHFINDING_H
#define LW_PATHFINDING_H

#include "lw_types.h"
#include "lw_map.h"

/*
 * A* shortest path from start_id to end_id.
 * - Returns path length (excluding start, including end), 0 if no path.
 * - out_path receives the cell ids in the order to walk (start excluded,
 *   end included). Sized at least LW_MAX_PATH_LEN.
 * - entity_at_cell != -1 cells are blocked, EXCEPT the end cell which
 *   is allowed.
 *
 * Returns 0 on no-path / invalid input.
 */
int lw_astar_path(const LwMap *map,
                  int start_id,
                  int end_id,
                  int *out_path,
                  int out_path_capacity);

/*
 * BFS-bounded reachability. Yields every walkable + unoccupied cell
 * reachable in [1, max_dist] steps from start_id, with its path.
 *
 * out_dest_ids[i]   -- cell id of the i-th destination
 * out_paths[i][k]   -- k-th cell on the path to dest i (start excluded)
 * out_path_lens[i]  -- length of path i
 *
 * Returns the number of destinations written (capped at max_dests).
 * Zero if start_id invalid or max_dist <= 0.
 */
int lw_bfs_reachable(const LwMap *map,
                     int start_id,
                     int max_dist,
                     int *out_dest_ids,
                     int (*out_paths)[LW_MAX_PATH_LEN],
                     int *out_path_lens,
                     int max_dests);

#endif /* LW_PATHFINDING_H */
