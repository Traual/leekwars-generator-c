/*
 * test_perf_smoke.c -- microbench for the full pipeline.
 *
 * Drives 1000 1v1 fights with the byte-for-byte attack pipeline,
 * end-to-end in pure C, to validate the engine isn't catastrophically
 * slow. This is a smoke benchmark, not a tuned one — but if it gets
 * accidentally regressed (e.g. an O(N^2) loop creeps into apply), the
 * runtime will balloon and we'll catch it.
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
#include <time.h>


static LwTopology g_topo;

static void build_15x15(void) {
    memset(&g_topo, 0, sizeof(g_topo));
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


static void register_weapon(void) {
    LwAttackSpec gun = {0};
    gun.attack_type = 1; gun.item_id = 1;
    gun.min_range = 1; gun.max_range = 12;
    gun.launch_type = 1;
    gun.area = LW_AREA_TYPE_SINGLE_CELL;
    gun.tp_cost = 4;
    gun.n_effects = 1;
    gun.effects[0].type = LW_EFFECT_DAMAGE;
    gun.effects[0].value1 = 80; gun.effects[0].value2 = 40;
    gun.effects[0].targets_filter = LW_TARGET_ENEMIES;
    lw_catalog_register(1, &gun);
}


static int run_one_fight(int seed) {
    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;
    s->n_entities = 2;
    for (int i = 0; i < 2; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i;
        s->entities[i].alive = 1;
        s->entities[i].hp = 800;
        s->entities[i].total_hp = 800;
        s->entities[i].base_stats[LW_STAT_TP] = 8;
        s->entities[i].base_stats[LW_STAT_STRENGTH] = 80;
        s->entities[i].base_stats[LW_STAT_FREQUENCY] = 100 + i;
        s->entities[i].n_weapons = 1;
        s->entities[i].weapons[0] = 1;
    }
    s->entities[0].team_id = 0;
    s->entities[0].cell_id = 7 * 15 + 3;
    s->map.entity_at_cell[s->entities[0].cell_id] = 0;
    s->entities[1].team_id = 1;
    s->entities[1].cell_id = 7 * 15 + 10;
    s->map.entity_at_cell[s->entities[1].cell_id] = 1;
    s->seed = seed;
    s->rng_n = (uint64_t)(int64_t)seed;
    lw_compute_start_order(s);

    int max_steps = 300;
    int turns = 0;
    while (turns < max_steps) {
        int active = lw_next_entity_turn(s);
        if (active < 0) break;
        lw_entity_start_turn(s, active);

        int target_idx = (active == 0) ? 1 : 0;
        if (s->entities[target_idx].alive) {
            LwAction a = {0};
            a.type = LW_ACTION_USE_WEAPON;
            a.weapon_id = 1;
            a.target_cell_id = s->entities[target_idx].cell_id;
            int avail_tp;
            do {
                avail_tp = (s->entities[active].base_stats[LW_STAT_TP]
                          + s->entities[active].buff_stats[LW_STAT_TP])
                          - s->entities[active].used_tp;
                if (avail_tp < 4 || !s->entities[target_idx].alive) break;
                lw_apply_action(s, active, &a);
            } while (1);
        }
        lw_entity_end_turn(s, active);
        if (lw_compute_winner(s, 0) != LW_WIN_ONGOING) break;
        turns++;
    }
    int w = lw_compute_winner(s, 0);
    lw_state_free(s);
    return w;
}


int main(void) {
    printf("test_perf_smoke:\n");
    build_15x15();
    register_weapon();

    int n = 1000;
    int wins[3] = {0, 0, 0};  /* team 0, team 1, draw */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < n; i++) {
        int w = run_one_fight(i + 1);
        if      (w == 0)  wins[0]++;
        else if (w == 1)  wins[1]++;
        else              wins[2]++;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double seconds = (t1.tv_sec - t0.tv_sec) +
                     (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double per = seconds * 1e6 / n;

    printf("   %d fights in %.2f s -> %.0f us/fight\n", n, seconds, per);
    printf("   team0 wins: %d, team1 wins: %d, draws: %d\n",
           wins[0], wins[1], wins[2]);

    /* Pass condition: under 5 ms per fight (very loose; on a modern
     * CPU we'd expect ~50-200 us). */
    int ok = (per < 5000.0);
    if (!ok) printf("   FAIL (perf regression)\n");
    return ok ? 0 : 1;
}
