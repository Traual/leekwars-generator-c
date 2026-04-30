/* test_los.c -- LoS + canUseAttack on a 5x5 grid. */

#include "lw_los.h"
#include "lw_map.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


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
            c->id = id; c->x = x; c->y = y;
            c->walkable = 1;
            topo->coord_lut[x][y] = id;
            topo->neighbors[id][0] = (y + 1 < 5) ? ((y + 1) * 5 + x) : -1;
            topo->neighbors[id][1] = (x - 1 >= 0) ? (y * 5 + x - 1)   : -1;
            topo->neighbors[id][2] = (y - 1 >= 0) ? ((y - 1) * 5 + x) : -1;
            topo->neighbors[id][3] = (x + 1 < 5) ? (y * 5 + x + 1)   : -1;
        }
    }
    for (int x = 0; x < LW_COORD_DIM; x++)
        for (int y = 0; y < LW_COORD_DIM; y++)
            if (x >= 5 || y >= 5) topo->coord_lut[x][y] = -1;
}


static void test_los_clear_line(void) {
    LwTopology topo;
    LwMap map;
    build_5x5(&topo);
    map.topo = &topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) map.entity_at_cell[i] = -1;

    int ig[1] = { 0 };
    /* (0,0) -> (4,0): horizontal clear */
    assert(lw_verify_los(&map, 0, 4, ig, 1, 1) == 1);
    /* (0,0) -> (0,4): vertical clear */
    assert(lw_verify_los(&map, 0, 20, ig, 1, 1) == 1);
    printf("  test_los_clear_line OK\n");
}


static void test_los_blocked_by_obstacle(void) {
    LwTopology topo;
    LwMap map;
    build_5x5(&topo);
    map.topo = &topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) map.entity_at_cell[i] = -1;

    /* Block (2,0) on the (0,0)->(4,0) line. */
    topo.cells[2].walkable = 0;
    int ig[1] = { 0 };
    assert(lw_verify_los(&map, 0, 4, ig, 1, 1) == 0);
    printf("  test_los_blocked_by_obstacle OK\n");
}


static void test_los_blocked_by_entity(void) {
    LwTopology topo;
    LwMap map;
    build_5x5(&topo);
    map.topo = &topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) map.entity_at_cell[i] = -1;

    /* Entity at (2,0) blocks LoS unless ignored. */
    map.entity_at_cell[2] = 99;
    int ig_just_start[1] = { 0 };
    assert(lw_verify_los(&map, 0, 4, ig_just_start, 1, 1) == 0);

    int ig_with_entity[2] = { 0, 2 };
    assert(lw_verify_los(&map, 0, 4, ig_with_entity, 2, 1) == 1);
    printf("  test_los_blocked_by_entity OK\n");
}


static void test_los_no_los_required(void) {
    LwTopology topo;
    LwMap map;
    build_5x5(&topo);
    map.topo = &topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) map.entity_at_cell[i] = -1;

    /* If need_los=0 the obstacle doesn't matter. */
    topo.cells[2].walkable = 0;
    int ig[1] = { 0 };
    assert(lw_verify_los(&map, 0, 4, ig, 1, 0) == 1);
    printf("  test_los_no_los_required OK\n");
}


static void test_can_use_attack(void) {
    LwTopology topo;
    LwMap map;
    build_5x5(&topo);
    map.topo = &topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) map.entity_at_cell[i] = -1;

    LwAttack atk = {
        .min_range = 2,
        .max_range = 5,
        .launch_type = 7,    /* line + diag + any */
        .needs_los = 1,
        .area = 0,
    };

    /* (0,0) -> (4,0): distance 4, horizontal: in range, line allowed, LoS clear */
    assert(lw_can_use_attack(&map, 0, 4, &atk) == 1);

    /* (0,0) -> (1,0): distance 1, below min_range */
    assert(lw_can_use_attack(&map, 0, 1, &atk) == 0);

    /* Restrict launch type to diag-only -- horizontal blocked */
    atk.launch_type = 2;
    assert(lw_can_use_attack(&map, 0, 4, &atk) == 0);

    /* (0,0) -> (3,3): diagonal, distance 6, exceeds max_range=5 */
    atk.launch_type = 7;
    assert(lw_can_use_attack(&map, 0, 18, &atk) == 0);  /* (3,3)=18, dist=6 */
    printf("  test_can_use_attack OK\n");
}


int main(void) {
    printf("test_los:\n");
    test_los_clear_line();
    test_los_blocked_by_obstacle();
    test_los_blocked_by_entity();
    test_los_no_los_required();
    test_can_use_attack();
    return 0;
}
