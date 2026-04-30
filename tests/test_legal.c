/* test_legal.c -- legal_actions on a 5x5 grid. */

#include "lw_legal.h"
#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


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


/* Two entities, on (0,0) and (3,0). The first has TP=10 MP=4, two
 * weapons, one chip on cooldown. */
static LwState* make_state_with_target(void) {
    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;

    s->n_entities = 2;
    LwEntity *e = &s->entities[0];
    memset(e, 0, sizeof(*e));
    e->id = 1; e->fid = 1; e->team_id = 0; e->alive = 1;
    e->base_stats[LW_STAT_TP] = 10;
    e->base_stats[LW_STAT_MP] = 4;
    e->cell_id = 0;
    e->n_weapons = 2;
    e->weapons[0] = 37; e->weapons[1] = 60;
    e->equipped_weapon = 0;
    e->n_chips = 1;
    e->chips[0] = 1;
    e->chip_cooldown[0] = 0;
    s->map.entity_at_cell[0] = 0;

    LwEntity *t = &s->entities[1];
    memset(t, 0, sizeof(*t));
    t->id = 2; t->fid = 2; t->team_id = 1; t->alive = 1;
    t->cell_id = 3;
    s->map.entity_at_cell[3] = 1;
    return s;
}


static LwInventoryProfile make_profile(void) {
    LwInventoryProfile p;
    memset(&p, 0, sizeof(p));
    /* Weapon 0: range 2..5, line+diag+any, no LoS, cost 5 (in range
     * for our (0,0)->(3,0) target). */
    p.weapon_costs[0] = 5;
    p.weapon_attacks[0].min_range = 2;
    p.weapon_attacks[0].max_range = 5;
    p.weapon_attacks[0].launch_type = 7;
    p.weapon_attacks[0].needs_los = 0;
    /* Weapon 1: melee 1..1, no LoS */
    p.weapon_costs[1] = 4;
    p.weapon_attacks[1].min_range = 1;
    p.weapon_attacks[1].max_range = 1;
    p.weapon_attacks[1].launch_type = 7;
    p.weapon_attacks[1].needs_los = 0;
    /* Chip 0: range 1..6, no LoS, cost 4 */
    p.chip_costs[0] = 4;
    p.chip_attacks[0].min_range = 1;
    p.chip_attacks[0].max_range = 6;
    p.chip_attacks[0].launch_type = 7;
    p.chip_attacks[0].needs_los = 0;
    return p;
}


static int count_actions(const LwAction *acts, int n, LwActionType t) {
    int c = 0;
    for (int i = 0; i < n; i++) if (acts[i].type == t) c++;
    return c;
}


static void test_basic_enum(void) {
    LwState *s = make_state_with_target();
    LwInventoryProfile p = make_profile();
    LwAction acts[1024];

    int n = lw_legal_actions(s, 0, &p, acts, 1024);

    /* END always present */
    assert(count_actions(acts, n, LW_ACTION_END) == 1);

    /* SET_WEAPON: switch to slot 1 (slot 0 is equipped) */
    int n_set = count_actions(acts, n, LW_ACTION_SET_WEAPON);
    assert(n_set == 1);

    /* USE_WEAPON: target at (3,0), in range 2..5, occupied -> 1 */
    int n_uw = count_actions(acts, n, LW_ACTION_USE_WEAPON);
    assert(n_uw == 1);

    /* USE_CHIP: target at (3,0) only (other cells empty) */
    int n_uc = count_actions(acts, n, LW_ACTION_USE_CHIP);
    assert(n_uc == 1);

    /* MOVE: BFS reachable cells within MP=4. From (0,0), the
     * reachable count excludes (3,0) (occupied). Manhattan ball
     * radius 4 within the 5x5 grid is 15 cells; minus 1 occupied
     * + 1 start = 13. */
    int n_mv = count_actions(acts, n, LW_ACTION_MOVE);
    assert(n_mv >= 10 && n_mv <= 15);

    lw_state_free(s);
    printf("  test_basic_enum OK (END=%d, SET=%d, UW=%d, UC=%d, MV=%d)\n",
           1, n_set, n_uw, n_uc, n_mv);
}


static void test_no_tp_no_attacks(void) {
    LwState *s = make_state_with_target();
    s->entities[0].used_tp = 10;  /* exhaust TP */
    LwInventoryProfile p = make_profile();
    LwAction acts[1024];
    int n = lw_legal_actions(s, 0, &p, acts, 1024);

    /* No SET_WEAPON, USE_WEAPON, USE_CHIP */
    assert(count_actions(acts, n, LW_ACTION_SET_WEAPON) == 0);
    assert(count_actions(acts, n, LW_ACTION_USE_WEAPON) == 0);
    assert(count_actions(acts, n, LW_ACTION_USE_CHIP) == 0);
    /* But MOVE + END still allowed */
    assert(count_actions(acts, n, LW_ACTION_END) == 1);
    assert(count_actions(acts, n, LW_ACTION_MOVE) > 0);

    lw_state_free(s);
    printf("  test_no_tp_no_attacks OK\n");
}


static void test_chip_cooldown(void) {
    LwState *s = make_state_with_target();
    s->entities[0].chip_cooldown[0] = 3;
    LwInventoryProfile p = make_profile();
    LwAction acts[1024];
    int n = lw_legal_actions(s, 0, &p, acts, 1024);
    assert(count_actions(acts, n, LW_ACTION_USE_CHIP) == 0);
    lw_state_free(s);
    printf("  test_chip_cooldown OK\n");
}


int main(void) {
    printf("test_legal:\n");
    build_5x5();
    test_basic_enum();
    test_no_tp_no_attacks();
    test_chip_cooldown();
    return 0;
}
