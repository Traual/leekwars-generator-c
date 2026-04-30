/*
 * test_area.c -- AoE cell enumeration parity.
 *
 * Mask-based shapes (Circle/Plus/X/Square 1..3) match Python's
 * MaskAreaCell.generate*Mask byte-for-byte. We verify the exact
 * sequence of cell ids by mapping (dx, dy) offsets back through the
 * topology, using a 15x15 grid centered on (7, 7) so even radius-3
 * shapes have no clipping.
 *
 * Dynamic shapes (LaserLine, FirstInLine, Allies, Enemies) are tested
 * separately with hand-placed entities.
 */

#include "lw_area.h"
#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


/* 15x15 grid, all cells walkable. Center cell at (7, 7) is id 7*15+7 = 112. */
static void build_15x15(LwTopology *topo) {
    memset(topo, 0, sizeof(*topo));
    topo->id = 1;
    topo->width = 15;
    topo->height = 15;
    topo->n_cells = 225;
    topo->min_x = 0; topo->max_x = 14;
    topo->min_y = 0; topo->max_y = 14;
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 15; x++) {
            int id = y * 15 + x;
            LwCell *c = &topo->cells[id];
            c->id = id; c->x = x; c->y = y;
            c->walkable = 1;
            topo->coord_lut[x][y] = id;
        }
    }
    for (int x = 0; x < LW_COORD_DIM; x++)
        for (int y = 0; y < LW_COORD_DIM; y++)
            if (x >= 15 || y >= 15) topo->coord_lut[x][y] = -1;
}


/* Convert a list of (dx, dy) offsets into expected cell ids assuming
 * target = (7, 7) on the 15x15 grid. */
static int mask_to_cells(const int (*mask)[2], int n, int *out) {
    int cx = 7, cy = 7;
    for (int i = 0; i < n; i++) {
        int x = cx + mask[i][0];
        int y = cy + mask[i][1];
        out[i] = y * 15 + x;
    }
    return n;
}


/* Compare two int sequences strictly. */
static int seq_eq(const int *a, const int *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}


static int run_mask_case(const char *name, int area_type,
                         const int (*expected_mask)[2], int expected_n) {
    LwTopology topo;
    build_15x15(&topo);

    int center = 7 * 15 + 7;
    int got[64];
    int got_n = lw_area_get_mask_cells(&topo, area_type, center, got, 64);

    int expected[64];
    mask_to_cells(expected_mask, expected_n, expected);

    int ok = (got_n == expected_n && seq_eq(got, expected, got_n));
    if (!ok) {
        printf("  %s: got_n=%d expected_n=%d\n", name, got_n, expected_n);
        printf("    got: ");
        for (int i = 0; i < got_n; i++) printf("%d ", got[i]);
        printf("\n    expected: ");
        for (int i = 0; i < expected_n; i++) printf("%d ", expected[i]);
        printf("\n");
    }
    return ok;
}


static int test_single_cell(void) {
    int mask[][2] = {{0, 0}};
    return run_mask_case("single_cell", LW_AREA_TYPE_SINGLE_CELL, mask, 1);
}

static int test_circle_1(void) {
    int mask[][2] = {{0,0}, {1,0}, {0,-1}, {-1,0}, {0,1}};
    return run_mask_case("circle_1", LW_AREA_TYPE_CIRCLE_1, mask, 5);
}

static int test_circle_2(void) {
    int mask[][2] = {
        {0,0}, {1,0}, {0,-1}, {-1,0}, {0,1},
        {2,0}, {1,-1}, {0,-2}, {-1,-1}, {-2,0},
        {-1,1}, {0,2}, {1,1}
    };
    return run_mask_case("circle_2", LW_AREA_TYPE_CIRCLE_2, mask, 13);
}

static int test_circle_3(void) {
    int mask[][2] = {
        {0,0}, {1,0}, {0,-1}, {-1,0}, {0,1},
        {2,0}, {1,-1}, {0,-2}, {-1,-1}, {-2,0},
        {-1,1}, {0,2}, {1,1},
        {3,0}, {2,-1}, {1,-2}, {0,-3}, {-1,-2},
        {-2,-1}, {-3,0}, {-2,1}, {-1,2}, {0,3}, {1,2}, {2,1}
    };
    return run_mask_case("circle_3", LW_AREA_TYPE_CIRCLE_3, mask, 25);
}

static int test_plus_2(void) {
    int mask[][2] = {
        {0,0}, {1,0}, {0,-1}, {-1,0}, {0,1},
        {2,0}, {0,-2}, {-2,0}, {0,2}
    };
    return run_mask_case("plus_2", LW_AREA_TYPE_PLUS_2, mask, 9);
}

static int test_plus_3(void) {
    int mask[][2] = {
        {0,0}, {1,0}, {0,-1}, {-1,0}, {0,1},
        {2,0}, {0,-2}, {-2,0}, {0,2},
        {3,0}, {0,-3}, {-3,0}, {0,3}
    };
    return run_mask_case("plus_3", LW_AREA_TYPE_PLUS_3, mask, 13);
}

static int test_x_1(void) {
    int mask[][2] = {{0,0}, {1,-1}, {-1,-1}, {-1,1}, {1,1}};
    return run_mask_case("x_1", LW_AREA_TYPE_X_1, mask, 5);
}

static int test_x_2(void) {
    int mask[][2] = {
        {0,0}, {1,-1}, {-1,-1}, {-1,1}, {1,1},
        {2,-2}, {-2,-2}, {-2,2}, {2,2}
    };
    return run_mask_case("x_2", LW_AREA_TYPE_X_2, mask, 9);
}

static int test_x_3(void) {
    int mask[][2] = {
        {0,0}, {1,-1}, {-1,-1}, {-1,1}, {1,1},
        {2,-2}, {-2,-2}, {-2,2}, {2,2},
        {3,-3}, {-3,-3}, {-3,3}, {3,3}
    };
    return run_mask_case("x_3", LW_AREA_TYPE_X_3, mask, 13);
}

static int test_square_1(void) {
    int mask[][2] = {
        {0,0}, {1,0}, {0,-1}, {-1,0}, {0,1},
        {1,-1}, {-1,-1}, {-1,1}, {1,1}
    };
    return run_mask_case("square_1", LW_AREA_TYPE_SQUARE_1, mask, 9);
}

static int test_square_2(void) {
    int mask[][2] = {
        {0,0}, {1,0}, {0,-1}, {-1,0}, {0,1},
        {2,0}, {1,-1}, {0,-2}, {-1,-1}, {-2,0},
        {-1,1}, {0,2}, {1,1},
        {2,-1}, {1,-2}, {-1,-2}, {-2,-1}, {-2,1},
        {-1,2}, {1,2}, {2,1},
        {2,-2}, {-2,-2}, {-2,2}, {2,2}
    };
    return run_mask_case("square_2", LW_AREA_TYPE_SQUARE_2, mask, 25);
}


static int test_mask_filters_offmap_and_obstacles(void) {
    /* Plus 2 centered at (1,1) -> (3,1), (1,-1) and (-1,1) are off-map.
     * (1,1) center, (2,1), (0,1), (1,0), (1,2) are on map. (1,-1) off,
     * (-1,1) off, (3,1) on map, (1,3) on map. */
    LwTopology topo;
    build_15x15(&topo);
    /* Mark (1, 0) as obstacle to test the walkable filter. */
    topo.cells[1].walkable = 0;
    int center = 1 * 15 + 1;  /* (1,1) */

    int got[64];
    int n = lw_area_get_mask_cells(&topo, LW_AREA_TYPE_PLUS_2, center, got, 64);
    /* Expected (in order): (1,1), (2,1), (0,1), (1,2), (3,1), (1,3).
     * Skipped: (1,0)=obstacle, (1,-1)=off, (-1,1)=off, (1,-2)=off. */
    int expected_xy[][2] = {{1,1}, {2,1}, {0,1}, {1,2}, {3,1}, {1,3}};
    int expected_n = 6;
    int expected[6];
    for (int i = 0; i < expected_n; i++) {
        expected[i] = expected_xy[i][1] * 15 + expected_xy[i][0];
    }
    int ok = (n == expected_n && seq_eq(got, expected, n));
    if (!ok) {
        printf("  filter: got_n=%d expected_n=%d\n", n, expected_n);
        printf("    got: ");
        for (int i = 0; i < n; i++) printf("%d ", got[i]);
        printf("\n    expected: ");
        for (int i = 0; i < expected_n; i++) printf("%d ", expected[i]);
        printf("\n");
    }
    return ok;
}


/* ---------------- dynamic shapes -------------------------------- */

static void setup_state_with_topo(LwState *s, LwTopology *topo) {
    s->n_entities = 0;
    s->map.topo = topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;
}


static int test_laser_line(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = lw_state_alloc();
    setup_state_with_topo(s, &topo);

    /* Caster at (3, 3); attack from (3,3) toward (3, 7). Line attack
     * with min_range=1, max_range=4, no LoS check. */
    LwAttack atk;
    memset(&atk, 0, sizeof(atk));
    atk.min_range = 1; atk.max_range = 4;
    atk.area = LW_AREA_TYPE_LASER_LINE;
    atk.needs_los = 0;
    int launch = 3 * 15 + 3;
    int target = 7 * 15 + 3;  /* (3, 7) */

    int got[16];
    int n = lw_area_get_cells(s, &atk, 0, launch, target, got, 16);
    /* Expected: (3,4), (3,5), (3,6), (3,7) -- 4 cells in order. */
    int expected[] = {
        4 * 15 + 3, 5 * 15 + 3, 6 * 15 + 3, 7 * 15 + 3
    };
    int ok = (n == 4 && seq_eq(got, expected, 4));
    if (!ok) {
        printf("  laser: got_n=%d  got: ", n);
        for (int i = 0; i < n; i++) printf("%d ", got[i]);
        printf("\n");
    }
    lw_state_free(s);
    return ok;
}


static int test_laser_line_blocked_by_los(void) {
    LwTopology topo;
    build_15x15(&topo);
    /* Block (3, 5). With LoS, the laser stops just before. */
    topo.cells[5 * 15 + 3].walkable = 0;
    LwState *s = lw_state_alloc();
    setup_state_with_topo(s, &topo);

    LwAttack atk;
    memset(&atk, 0, sizeof(atk));
    atk.min_range = 1; atk.max_range = 4;
    atk.area = LW_AREA_TYPE_LASER_LINE;
    atk.needs_los = 1;
    int launch = 3 * 15 + 3;
    int target = 7 * 15 + 3;

    int got[16];
    int n = lw_area_get_cells(s, &atk, 0, launch, target, got, 16);
    int expected[] = { 4 * 15 + 3 };  /* (3,4) only; (3,5) blocked */
    int ok = (n == 1 && seq_eq(got, expected, 1));
    if (!ok) {
        printf("  laser_blocked: got_n=%d -> FAIL\n", n);
    }
    lw_state_free(s);
    return ok;
}


static int test_first_in_line(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = lw_state_alloc();
    setup_state_with_topo(s, &topo);
    /* Entities: 0=caster at (3,3), 1=blocker at (3, 6), 2=behind at (3, 8). */
    s->n_entities = 3;
    for (int i = 0; i < 3; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i; s->entities[i].alive = 1;
    }
    s->entities[0].cell_id = 3 * 15 + 3; s->entities[0].team_id = 0;
    s->entities[1].cell_id = 6 * 15 + 3; s->entities[1].team_id = 1;
    s->entities[2].cell_id = 8 * 15 + 3; s->entities[2].team_id = 1;
    s->map.entity_at_cell[s->entities[0].cell_id] = 0;
    s->map.entity_at_cell[s->entities[1].cell_id] = 1;
    s->map.entity_at_cell[s->entities[2].cell_id] = 2;

    LwAttack atk;
    memset(&atk, 0, sizeof(atk));
    atk.min_range = 1; atk.max_range = 6;
    atk.area = LW_AREA_TYPE_FIRST_IN_LINE;
    int launch = 3 * 15 + 3;
    int target = 8 * 15 + 3;

    int got[4];
    int n = lw_area_get_cells(s, &atk, 0, launch, target, got, 4);
    /* Should hit the closer blocker only. */
    int ok = (n == 1 && got[0] == 6 * 15 + 3);
    if (!ok) {
        printf("  first_in_line: got_n=%d got=%d -> FAIL\n",
               n, n > 0 ? got[0] : -1);
    }
    lw_state_free(s);
    return ok;
}


static int test_first_in_line_skips_dead(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = lw_state_alloc();
    setup_state_with_topo(s, &topo);
    s->n_entities = 3;
    for (int i = 0; i < 3; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i;
    }
    /* caster alive, dead blocker, live target. The dead blocker should
     * not be picked up since alive==0. */
    s->entities[0].cell_id = 3 * 15 + 3; s->entities[0].alive = 1;
    s->entities[1].cell_id = 6 * 15 + 3; s->entities[1].alive = 0;
    s->entities[2].cell_id = 8 * 15 + 3; s->entities[2].alive = 1;
    s->map.entity_at_cell[s->entities[1].cell_id] = 1;
    s->map.entity_at_cell[s->entities[2].cell_id] = 2;

    LwAttack atk;
    memset(&atk, 0, sizeof(atk));
    atk.min_range = 1; atk.max_range = 6;
    atk.area = LW_AREA_TYPE_FIRST_IN_LINE;
    int launch = 3 * 15 + 3;
    int target = 8 * 15 + 3;

    int got[4];
    int n = lw_area_get_cells(s, &atk, 0, launch, target, got, 4);
    int ok = (n == 1 && got[0] == 8 * 15 + 3);
    if (!ok) {
        printf("  first_in_line_dead: got_n=%d -> FAIL\n", n);
    }
    lw_state_free(s);
    return ok;
}


static int test_enemies(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = lw_state_alloc();
    setup_state_with_topo(s, &topo);
    s->n_entities = 4;
    for (int i = 0; i < 4; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i; s->entities[i].alive = 1;
    }
    s->entities[0].cell_id = 1; s->entities[0].team_id = 0;  /* caster */
    s->entities[1].cell_id = 2; s->entities[1].team_id = 0;  /* ally */
    s->entities[2].cell_id = 3; s->entities[2].team_id = 1;  /* enemy 1 */
    s->entities[3].cell_id = 4; s->entities[3].team_id = 1;  /* enemy 2 */

    LwAttack atk;
    memset(&atk, 0, sizeof(atk));
    atk.area = LW_AREA_TYPE_ENEMIES;
    int got[8];
    int n = lw_area_get_cells(s, &atk, 0, /*launch*/0, /*target*/0, got, 8);
    int ok = (n == 2 && got[0] == 3 && got[1] == 4);
    if (!ok) printf("  enemies: got_n=%d -> FAIL\n", n);
    lw_state_free(s);
    return ok;
}


static int test_allies(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = lw_state_alloc();
    setup_state_with_topo(s, &topo);
    s->n_entities = 4;
    for (int i = 0; i < 4; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i; s->entities[i].alive = 1;
    }
    s->entities[0].cell_id = 1; s->entities[0].team_id = 0;
    s->entities[1].cell_id = 2; s->entities[1].team_id = 0;
    s->entities[2].cell_id = 3; s->entities[2].team_id = 1;
    s->entities[3].cell_id = 4; s->entities[3].team_id = 0;

    LwAttack atk;
    memset(&atk, 0, sizeof(atk));
    atk.area = LW_AREA_TYPE_ALLIES;
    int got[8];
    int n = lw_area_get_cells(s, &atk, 0, 0, 0, got, 8);
    /* Caster is on team 0, allies are entities 0, 1, 3 -> cells 1, 2, 4. */
    int ok = (n == 3 && got[0] == 1 && got[1] == 2 && got[2] == 4);
    if (!ok) printf("  allies: got_n=%d -> FAIL\n", n);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_area:\n");
    int n = 0, ok = 0;
    n++; if (test_single_cell())                       { printf("   1  single_cell OK\n"); ok++; }
    n++; if (test_circle_1())                          { printf("   2  circle_1 OK\n"); ok++; }
    n++; if (test_circle_2())                          { printf("   3  circle_2 OK\n"); ok++; }
    n++; if (test_circle_3())                          { printf("   4  circle_3 OK\n"); ok++; }
    n++; if (test_plus_2())                            { printf("   5  plus_2 OK\n"); ok++; }
    n++; if (test_plus_3())                            { printf("   6  plus_3 OK\n"); ok++; }
    n++; if (test_x_1())                               { printf("   7  x_1 OK\n"); ok++; }
    n++; if (test_x_2())                               { printf("   8  x_2 OK\n"); ok++; }
    n++; if (test_x_3())                               { printf("   9  x_3 OK\n"); ok++; }
    n++; if (test_square_1())                          { printf("  10  square_1 OK\n"); ok++; }
    n++; if (test_square_2())                          { printf("  11  square_2 OK\n"); ok++; }
    n++; if (test_mask_filters_offmap_and_obstacles()) { printf("  12  mask_filters OK\n"); ok++; }
    n++; if (test_laser_line())                        { printf("  13  laser_line OK\n"); ok++; }
    n++; if (test_laser_line_blocked_by_los())         { printf("  14  laser_line_blocked OK\n"); ok++; }
    n++; if (test_first_in_line())                     { printf("  15  first_in_line OK\n"); ok++; }
    n++; if (test_first_in_line_skips_dead())          { printf("  16  first_in_line_dead OK\n"); ok++; }
    n++; if (test_enemies())                           { printf("  17  enemies OK\n"); ok++; }
    n++; if (test_allies())                            { printf("  18  allies OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
