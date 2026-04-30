/* test_action.c -- apply_action for END, SET_WEAPON, MOVE. */

#include "lw_action.h"
#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


/* Tiny 5x5 grid topology used as the entity's playground. */
static LwTopology g_topo;

static void build_5x5(void) {
    memset(&g_topo, 0, sizeof(g_topo));
    g_topo.id = 1;
    g_topo.width = 5;
    g_topo.height = 5;
    g_topo.n_cells = 25;
    g_topo.min_x = 0; g_topo.max_x = 4;
    g_topo.min_y = 0; g_topo.max_y = 4;
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            int id = y * 5 + x;
            LwCell *c = &g_topo.cells[id];
            c->id = id; c->x = x; c->y = y; c->walkable = 1;
            g_topo.coord_lut[x][y] = id;
            g_topo.neighbors[id][0] = (y + 1 < 5) ? ((y + 1) * 5 + x) : -1;
            g_topo.neighbors[id][1] = (x - 1 >= 0) ? (y * 5 + x - 1)   : -1;
            g_topo.neighbors[id][2] = (y - 1 >= 0) ? ((y - 1) * 5 + x) : -1;
            g_topo.neighbors[id][3] = (x + 1 < 5) ? (y * 5 + x + 1)   : -1;
        }
    }
    for (int x = 0; x < LW_COORD_DIM; x++)
        for (int y = 0; y < LW_COORD_DIM; y++)
            if (x >= 5 || y >= 5) g_topo.coord_lut[x][y] = -1;
}


/* Build a minimal state with one entity at cell 0, TP=10, MP=5. */
static LwState* make_simple_state(void) {
    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;

    s->n_entities = 1;
    LwEntity *e = &s->entities[0];
    memset(e, 0, sizeof(*e));
    e->id = 1; e->fid = 1; e->team_id = 0;
    e->hp = 1000; e->total_hp = 1000;
    e->base_stats[LW_STAT_TP] = 10;
    e->base_stats[LW_STAT_MP] = 5;
    e->cell_id = 0;
    s->map.entity_at_cell[0] = 0;
    e->n_weapons = 2;
    e->weapons[0] = 37;
    e->weapons[1] = 60;
    e->equipped_weapon = 0;
    e->alive = 1;
    return s;
}


static void test_end_is_noop(void) {
    LwState *s = make_simple_state();
    LwAction a; lw_action_init(&a, LW_ACTION_END);
    assert(lw_apply_action(s, 0, &a) == 1);
    assert(s->entities[0].used_tp == 0);
    assert(s->entities[0].used_mp == 0);
    lw_state_free(s);
    printf("  test_end_is_noop OK\n");
}


static void test_set_weapon(void) {
    LwState *s = make_simple_state();
    LwAction a; lw_action_init(&a, LW_ACTION_SET_WEAPON);
    a.weapon_id = 60;

    assert(lw_apply_action(s, 0, &a) == 1);
    assert(s->entities[0].equipped_weapon == 1);
    assert(s->entities[0].used_tp == 1);

    /* No-op switch (already equipped) returns 0 */
    a.weapon_id = 60;
    assert(lw_apply_action(s, 0, &a) == 0);
    assert(s->entities[0].used_tp == 1);

    /* Unknown weapon id rejected */
    a.weapon_id = 999;
    assert(lw_apply_action(s, 0, &a) == 0);

    lw_state_free(s);
    printf("  test_set_weapon OK\n");
}


static void test_move(void) {
    LwState *s = make_simple_state();
    LwAction a; lw_action_init(&a, LW_ACTION_MOVE);
    /* Walk (0,0) -> (1,0) -> (2,0) -> (3,0): 3 steps, ids 1,2,3 */
    a.path[0] = 1; a.path[1] = 2; a.path[2] = 3;
    a.path_len = 3;
    a.target_cell_id = 3;

    assert(lw_apply_action(s, 0, &a) == 1);
    assert(s->entities[0].cell_id == 3);
    assert(s->entities[0].used_mp == 3);
    assert(s->map.entity_at_cell[0] == -1);  /* old cell freed */
    assert(s->map.entity_at_cell[3] == 0);   /* new cell holds entity index */
    assert(s->map.entity_at_cell[1] == -1);
    assert(s->map.entity_at_cell[2] == -1);
    lw_state_free(s);
    printf("  test_move OK\n");
}


static void test_move_too_long(void) {
    LwState *s = make_simple_state();
    LwAction a; lw_action_init(&a, LW_ACTION_MOVE);
    /* Try to walk 6 cells with MP=5 -- should be rejected. */
    for (int i = 0; i < 6; i++) a.path[i] = i + 1;
    a.path_len = 6;

    assert(lw_apply_action(s, 0, &a) == 0);
    assert(s->entities[0].cell_id == 0);  /* unchanged */
    assert(s->entities[0].used_mp == 0);
    lw_state_free(s);
    printf("  test_move_too_long OK\n");
}


static void test_move_blocked_by_entity(void) {
    LwState *s = make_simple_state();

    /* Add a second entity at cell 2. */
    s->n_entities = 2;
    LwEntity *e2 = &s->entities[1];
    memset(e2, 0, sizeof(*e2));
    e2->id = 2; e2->fid = 2; e2->team_id = 1;
    e2->cell_id = 2;
    e2->alive = 1;
    s->map.entity_at_cell[2] = 1;

    LwAction a; lw_action_init(&a, LW_ACTION_MOVE);
    a.path[0] = 1; a.path[1] = 2;
    a.path_len = 2;
    /* Should fail: cell 2 is occupied by entity index 1. */
    assert(lw_apply_action(s, 0, &a) == 0);
    /* No partial state mutation: original entity stayed at cell 0. */
    assert(s->entities[0].cell_id == 0);
    assert(s->entities[0].used_mp == 0);
    lw_state_free(s);
    printf("  test_move_blocked_by_entity OK\n");
}


static void test_dispatch_dead_rejected(void) {
    LwState *s = make_simple_state();
    s->entities[0].alive = 0;
    LwAction a; lw_action_init(&a, LW_ACTION_END);
    assert(lw_apply_action(s, 0, &a) == 0);
    lw_state_free(s);
    printf("  test_dispatch_dead_rejected OK\n");
}


int main(void) {
    printf("test_action:\n");
    build_5x5();
    test_end_is_noop();
    test_set_weapon();
    test_move();
    test_move_too_long();
    test_move_blocked_by_entity();
    test_dispatch_dead_rejected();
    return 0;
}
