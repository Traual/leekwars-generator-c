/*
 * test_pathfinding.c -- A* and BFS on a synthetic 5x5 grid.
 *
 * No need to load a real LeekWars map for this layer's tests --
 * we hand-build a topology where adjacencies are obvious, then
 * assert paths and reachable sets directly.
 */

#include "lw_pathfinding.h"
#include "lw_map.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


/* Build a 5x5 grid topology in row-major order: id = y * 5 + x.
 * Neighbours = 4-connected (S, W, N, E) with -1 outside. */
static void build_5x5(LwTopology *topo) {
    memset(topo, 0, sizeof(*topo));
    topo->id = 1;
    topo->width = 5;
    topo->height = 5;
    topo->n_cells = 25;
    topo->min_x = 0;
    topo->max_x = 4;
    topo->min_y = 0;
    topo->max_y = 4;

    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            int id = y * 5 + x;
            LwCell *c = &topo->cells[id];
            c->id = id;
            c->x = x;
            c->y = y;
            c->walkable = 1;
            c->obstacle_size = 0;
            c->composante = 0;
            topo->coord_lut[x][y] = id;

            /* Neighbours: S = (y+1), W = (x-1), N = (y-1), E = (x+1) */
            topo->neighbors[id][0] = (y + 1 < 5) ? ((y + 1) * 5 + x) : -1;
            topo->neighbors[id][1] = (x - 1 >= 0) ? (y * 5 + (x - 1))   : -1;
            topo->neighbors[id][2] = (y - 1 >= 0) ? ((y - 1) * 5 + x)   : -1;
            topo->neighbors[id][3] = (x + 1 < 5) ? (y * 5 + (x + 1))   : -1;
        }
    }
    /* Fill remaining coord_lut with -1. */
    for (int x = 0; x < LW_COORD_DIM; x++) {
        for (int y = 0; y < LW_COORD_DIM; y++) {
            if (x >= 5 || y >= 5) topo->coord_lut[x][y] = -1;
        }
    }
}


static void init_map_empty(LwMap *map, const LwTopology *topo) {
    map->topo = topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) {
        map->entity_at_cell[i] = -1;
    }
}


static void test_astar_straight(void) {
    LwTopology topo;
    LwMap map;
    int path[LW_MAX_PATH_LEN];

    build_5x5(&topo);
    init_map_empty(&map, &topo);

    /* (0,0) -> (4,0): straight east, 4 steps */
    int len = lw_astar_path(&map, 0, 4, path, LW_MAX_PATH_LEN);
    assert(len == 4);
    /* path should be [(1,0), (2,0), (3,0), (4,0)] = ids 1, 2, 3, 4 */
    assert(path[0] == 1);
    assert(path[1] == 2);
    assert(path[2] == 3);
    assert(path[3] == 4);
    printf("  test_astar_straight OK\n");
}


static void test_astar_around_obstacle(void) {
    LwTopology topo;
    LwMap map;
    int path[LW_MAX_PATH_LEN];

    build_5x5(&topo);
    init_map_empty(&map, &topo);

    /* Block the middle cell (2,2) = id 12 */
    topo.cells[12].walkable = 0;

    /* Path (0,0) -> (4,4) should still be 8 steps long (manhattan dist) */
    int len = lw_astar_path(&map, 0, 24, path, LW_MAX_PATH_LEN);
    assert(len == 8);
    /* And cell 12 must NOT appear on the path */
    for (int i = 0; i < len; i++) {
        assert(path[i] != 12);
    }
    printf("  test_astar_around_obstacle OK\n");
}


static void test_astar_blocked_by_entity(void) {
    LwTopology topo;
    LwMap map;
    int path[LW_MAX_PATH_LEN];

    build_5x5(&topo);
    init_map_empty(&map, &topo);

    /* Entity at (1,0) = id 1 blocks (0,0)->(4,0) direct route */
    map.entity_at_cell[1] = 99;

    int len = lw_astar_path(&map, 0, 4, path, LW_MAX_PATH_LEN);
    assert(len > 4);  /* must detour */
    for (int i = 0; i < len - 1; i++) {  /* end allowed even if occupied */
        assert(path[i] != 1);
    }
    printf("  test_astar_blocked_by_entity OK\n");
}


static void test_bfs_reachable_full(void) {
    LwTopology topo;
    LwMap map;
    int dest_ids[LW_MAX_CELLS];
    int paths[LW_MAX_CELLS][LW_MAX_PATH_LEN];
    int path_lens[LW_MAX_CELLS];

    build_5x5(&topo);
    init_map_empty(&map, &topo);

    /* From center (2,2)=id 12, max_dist=8 should reach every other cell */
    int n = lw_bfs_reachable(&map, 12, 8, dest_ids, paths, path_lens, LW_MAX_CELLS);
    /* 24 = 25 cells - start = reachable (no obstacles) */
    assert(n == 24);
    printf("  test_bfs_reachable_full OK\n");
}


static void test_bfs_reachable_bounded(void) {
    LwTopology topo;
    LwMap map;
    int dest_ids[LW_MAX_CELLS];
    int paths[LW_MAX_CELLS][LW_MAX_PATH_LEN];
    int path_lens[LW_MAX_CELLS];

    build_5x5(&topo);
    init_map_empty(&map, &topo);

    /* From (2,2)=id 12 with max_dist=2: cells reachable in <=2 steps.
     * Manhattan ball: 1 (center), 4 (dist 1), 8 (dist 2) = 13 cells.
     * Excluding start: 12 destinations. */
    int n = lw_bfs_reachable(&map, 12, 2, dest_ids, paths, path_lens, LW_MAX_CELLS);
    assert(n == 12);
    /* Verify no path exceeds max_dist */
    for (int i = 0; i < n; i++) {
        assert(path_lens[i] >= 1 && path_lens[i] <= 2);
    }
    printf("  test_bfs_reachable_bounded OK\n");
}


int main(void) {
    printf("test_pathfinding:\n");
    test_astar_straight();
    test_astar_around_obstacle();
    test_astar_blocked_by_entity();
    test_bfs_reachable_full();
    test_bfs_reachable_bounded();
    return 0;
}
