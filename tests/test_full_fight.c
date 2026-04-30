/*
 * test_full_fight.c -- end-to-end self-play smoke test.
 *
 * Builds a 1v1 with a single weapon, drives the fight loop manually
 * (no AI -- just always shoot the enemy), and checks the engine
 * runs through to a winner without crashing.
 *
 * This is the integration test that proves all the pieces fit
 * together: start_order, next_entity_turn, entity_start_turn,
 * apply_action -> attack_pipeline -> effect_dispatch ->
 * apply_damage + erosion, entity_end_turn, compute_winner.
 */

#include "lw_action.h"
#include "lw_attack_apply.h"
#include "lw_area.h"
#include "lw_catalog.h"
#include "lw_order.h"
#include "lw_state.h"
#include "lw_turn.h"
#include "lw_winner.h"
#include <stdio.h>
#include <string.h>


static LwTopology g_topo;

static void build_15x15(void) {
    memset(&g_topo, 0, sizeof(g_topo));
    g_topo.id = 1;
    g_topo.width = 15; g_topo.height = 15;
    g_topo.n_cells = 225;
    g_topo.min_x = 0; g_topo.max_x = 14;
    g_topo.min_y = 0; g_topo.max_y = 14;
    for (int y = 0; y < 15; y++)
        for (int x = 0; x < 15; x++) {
            int id = y * 15 + x;
            g_topo.cells[id].id = id;
            g_topo.cells[id].x = x;
            g_topo.cells[id].y = y;
            g_topo.cells[id].walkable = 1;
            g_topo.coord_lut[x][y] = id;
        }
    for (int x = 0; x < LW_COORD_DIM; x++)
        for (int y = 0; y < LW_COORD_DIM; y++)
            if (x >= 15 || y >= 15) g_topo.coord_lut[x][y] = -1;
}


static int test_simple_1v1_runs_to_winner(void) {
    build_15x15();
    lw_catalog_clear();

    /* Register a single weapon: 4 TP, 100 base damage, range 1-12. */
    LwAttackSpec gun = {0};
    gun.attack_type = 1;
    gun.item_id = 1;
    gun.min_range = 1; gun.max_range = 12;
    gun.launch_type = 1;
    gun.area = LW_AREA_TYPE_SINGLE_CELL;
    gun.tp_cost = 4;
    gun.n_effects = 1;
    gun.effects[0].type = LW_EFFECT_DAMAGE;
    gun.effects[0].value1 = 100;
    gun.effects[0].value2 = 50;
    gun.effects[0].targets_filter = LW_TARGET_ENEMIES;
    lw_catalog_register(1, &gun);

    /* Two entities at (3, 7) and (10, 7). HP 500 each, 100 strength. */
    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;
    s->n_entities = 2;
    for (int i = 0; i < 2; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i;
        s->entities[i].alive = 1;
        s->entities[i].hp = 500;
        s->entities[i].total_hp = 500;
        s->entities[i].base_stats[LW_STAT_TP] = 8;
        s->entities[i].base_stats[LW_STAT_MP] = 0;
        s->entities[i].base_stats[LW_STAT_STRENGTH] = 100;
        s->entities[i].base_stats[LW_STAT_FREQUENCY] = 100 + i;
        s->entities[i].n_weapons = 1;
        s->entities[i].weapons[0] = 1;
        s->entities[i].equipped_weapon = 0;
    }
    s->entities[0].team_id = 0;
    s->entities[0].cell_id = 7 * 15 + 3;
    s->map.entity_at_cell[s->entities[0].cell_id] = 0;
    s->entities[1].team_id = 1;
    s->entities[1].cell_id = 7 * 15 + 10;
    s->map.entity_at_cell[s->entities[1].cell_id] = 1;
    s->seed = 42;
    s->rng_n = 42;

    /* Compute initial order. */
    lw_compute_start_order(s);

    /* Drive fight: each entity shoots the other until someone dies. */
    int max_steps = 200;
    int step = 0;
    int winner = LW_WIN_ONGOING;
    while (step < max_steps) {
        int active = lw_next_entity_turn(s);
        if (active < 0) break;

        lw_entity_start_turn(s, active);

        /* Single action: shoot the other entity. */
        int target_idx = (active == 0) ? 1 : 0;
        if (s->entities[target_idx].alive) {
            LwAction a = {0};
            a.type = LW_ACTION_USE_WEAPON;
            a.weapon_id = 1;
            a.target_cell_id = s->entities[target_idx].cell_id;
            int avail_tp = (s->entities[active].base_stats[LW_STAT_TP]
                          + s->entities[active].buff_stats[LW_STAT_TP])
                          - s->entities[active].used_tp;
            while (avail_tp >= gun.tp_cost && s->entities[target_idx].alive) {
                lw_apply_action(s, active, &a);
                avail_tp -= gun.tp_cost;
            }
        }

        lw_entity_end_turn(s, active);

        winner = lw_compute_winner(s, 0);
        if (winner != LW_WIN_ONGOING) break;
        step++;
    }

    int ok = (winner == 0 || winner == 1) &&
             (s->entities[0].alive ^ s->entities[1].alive) /* exactly one alive */;
    if (!ok) {
        printf("  full_fight: winner=%d step=%d e0.hp=%d e1.hp=%d -> FAIL\n",
               winner, step, s->entities[0].hp, s->entities[1].hp);
    } else {
        printf("    fight ended after %d steps; winner=team%d "
               "(survivor hp=%d/%d)\n",
               step, winner,
               s->entities[winner == 0 ? 0 : 1].hp,
               s->entities[winner == 0 ? 0 : 1].total_hp);
    }
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_full_fight:\n");
    int n = 0, ok = 0;
    n++; if (test_simple_1v1_runs_to_winner()) { printf("   1  simple_1v1 OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
