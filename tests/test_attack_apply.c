/*
 * test_attack_apply.c -- end-to-end attack pipeline.
 *
 * The pipeline:
 *   1. roll critical (consumes 1 RNG draw)
 *   2. roll jet     (consumes 1 RNG draw)
 *   3. enumerate area cells
 *   4. apply each effect to each entity in the area
 *   5. handle Push/Attract/Teleport short-circuits
 *
 * We seed the RNG so we know which (critical, jet) values come out,
 * then verify the resulting HP / buff_stats deltas match what the
 * apply_* functions would produce on those exact rolls.
 */

#include "lw_attack_apply.h"
#include "lw_area.h"
#include "lw_state.h"
#include "lw_rng.h"
#include "lw_critical.h"
#include "lw_effect.h"
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


static LwState* fresh_two_entities(int seed) {
    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;

    s->n_entities = 2;
    /* Caster at (3, 7), Target at (10, 7). */
    LwEntity *c = &s->entities[0];
    LwEntity *t = &s->entities[1];
    memset(c, 0, sizeof(*c));
    memset(t, 0, sizeof(*t));
    c->id = 0; c->team_id = 0; c->alive = 1;
    c->cell_id = 7 * 15 + 3;
    c->hp = 1000; c->total_hp = 1000;
    c->base_stats[LW_STAT_TP] = 20;
    c->base_stats[LW_STAT_MP] = 5;
    s->map.entity_at_cell[c->cell_id] = 0;

    t->id = 1; t->team_id = 1; t->alive = 1;
    t->cell_id = 7 * 15 + 10;
    t->hp = 1000; t->total_hp = 1000;
    s->map.entity_at_cell[t->cell_id] = 1;

    s->rng_n = (uint64_t)(int64_t)seed;
    return s;
}


static int test_simple_damage_attack(void) {
    /* Single-cell, single-effect attack. v1=80, v2=120, str=100. */
    build_15x15();
    LwState *s = fresh_two_entities(42);
    s->entities[0].base_stats[LW_STAT_STRENGTH] = 100;

    /* seed=42 first 2 draws: 0.7911 (critical roll), 0.2599 (jet).
     * Caster has 0 agility so threshold=0, never crits.
     * Expected damage with jet=0.2599...:
     *   d = (80 + 0.2599*120) * (1 + 100/100) * 1 * 1 * 1 * (1+0/100)
     *     = (80 + 31.190) * 2
     *     = 111.190 * 2 = 222.38 -> round = 222.
     * Erosion = round(222 * 0.05) = 11. */
    LwAttackSpec atk = {0};
    atk.attack_type = 1;
    atk.item_id = 100;
    atk.min_range = 1; atk.max_range = 12;
    atk.launch_type = 1;
    atk.area = LW_AREA_TYPE_SINGLE_CELL;
    atk.needs_los = 1;
    atk.tp_cost = 5;
    atk.n_effects = 1;
    atk.effects[0].type = LW_EFFECT_DAMAGE;
    atk.effects[0].value1 = 80;
    atk.effects[0].value2 = 120;
    atk.effects[0].turns = 0;
    atk.effects[0].targets_filter = LW_TARGET_ENEMIES;

    int dealt = lw_apply_attack_full(s, 0, s->entities[1].cell_id, &atk);
    int ok = (dealt == 222 &&
              s->entities[1].hp == 778 &&
              s->entities[1].total_hp == 989);
    if (!ok) printf("  damage_attack: dealt=%d hp=%d total=%d -> FAIL\n",
                    dealt, s->entities[1].hp, s->entities[1].total_hp);
    lw_state_free(s);
    return ok;
}


static int test_buff_attack_registers_entry(void) {
    /* Self-cast buff: buff_strength turn=3, science=100. */
    build_15x15();
    LwState *s = fresh_two_entities(42);
    s->entities[0].base_stats[LW_STAT_SCIENCE] = 100;

    LwAttackSpec atk = {0};
    atk.area = LW_AREA_TYPE_SINGLE_CELL;
    atk.min_range = 0; atk.max_range = 0;
    atk.launch_type = 1;
    atk.n_effects = 1;
    atk.effects[0].type = LW_EFFECT_BUFF_STRENGTH;
    atk.effects[0].value1 = 30;
    atk.effects[0].value2 = 30;
    atk.effects[0].turns = 3;
    atk.effects[0].targets_filter = LW_TARGET_CASTER;

    /* Target the caster's own cell. */
    int dealt = lw_apply_attack_full(s, 0, s->entities[0].cell_id, &atk);
    /* No damage tracked for buffs. */
    LwEntity *c = &s->entities[0];
    int ok = (dealt == 0 &&
              c->n_effects == 1 &&
              c->effects[0].id == LW_EFFECT_BUFF_STRENGTH &&
              c->effects[0].turns == 3 &&
              c->buff_stats[LW_STAT_STRENGTH] > 0);
    if (!ok) printf("  buff_attack: dealt=%d n=%d str=%d -> FAIL\n",
                    dealt, c->n_effects, c->buff_stats[LW_STAT_STRENGTH]);
    lw_state_free(s);
    return ok;
}


static int test_aoe_circle_hits_all(void) {
    /* CIRCLE_1 around target — caster + 4 cardinals + target.
     * Place an extra entity at (10, 8) so it gets hit alongside the
     * primary target at (10, 7). The extra is on team 1 (enemy). */
    build_15x15();
    LwState *s = fresh_two_entities(42);
    s->entities[0].base_stats[LW_STAT_STRENGTH] = 0;  /* no buffs */

    /* Add a third entity at (10, 8). */
    s->n_entities = 3;
    LwEntity *e3 = &s->entities[2];
    memset(e3, 0, sizeof(*e3));
    e3->id = 2; e3->team_id = 1; e3->alive = 1;
    e3->cell_id = 8 * 15 + 10;  /* adjacent below target */
    e3->hp = 1000; e3->total_hp = 1000;
    s->map.entity_at_cell[e3->cell_id] = 2;

    LwAttackSpec atk = {0};
    atk.area = LW_AREA_TYPE_CIRCLE_1;
    atk.min_range = 1; atk.max_range = 12;
    atk.launch_type = 1;
    atk.n_effects = 1;
    atk.effects[0].type = LW_EFFECT_DAMAGE;
    atk.effects[0].value1 = 100;
    atk.effects[0].value2 = 0;
    atk.effects[0].targets_filter = LW_TARGET_ENEMIES;

    int dealt = lw_apply_attack_full(s, 0, s->entities[1].cell_id, &atk);

    /* Target gets hit at center (aoe=1.0), e3 at adjacent (aoe=0.8).
     * Damage calc:
     *   For target (aoe=1.0): d = 100 * 1 * 1 * 1 * 1 * 1 = 100.
     *   Erosion = round(100 * 0.05) = 5. hp 1000->900, total 1000->995.
     *   For e3 (aoe=0.8): d = 100 * 1 * 0.8 * 1 * 1 * 1 = 80.
     *   Erosion = round(80 * 0.05) = 4. hp 1000->920, total 1000->996.
     * Total dealt = 100 + 80 = 180.
     */
    int ok = (dealt == 180 &&
              s->entities[1].hp == 900 &&
              s->entities[1].total_hp == 995 &&
              s->entities[2].hp == 920 &&
              s->entities[2].total_hp == 996);
    if (!ok) printf("  aoe: dealt=%d t.hp=%d e3.hp=%d -> FAIL\n",
                    dealt, s->entities[1].hp, s->entities[2].hp);
    lw_state_free(s);
    return ok;
}


static int test_critical_doubles_damage(void) {
    /* Caster has agility=1000 -> always crits.
     * Critical multiplies damage by 1.3, plus erosion +0.10. */
    build_15x15();
    LwState *s = fresh_two_entities(42);
    s->entities[0].base_stats[LW_STAT_STRENGTH] = 0;
    s->entities[0].base_stats[LW_STAT_AGILITY] = 1000;

    LwAttackSpec atk = {0};
    atk.area = LW_AREA_TYPE_SINGLE_CELL;
    atk.min_range = 1; atk.max_range = 12;
    atk.launch_type = 1;
    atk.n_effects = 1;
    atk.effects[0].type = LW_EFFECT_DAMAGE;
    atk.effects[0].value1 = 100;
    atk.effects[0].value2 = 0;
    atk.effects[0].targets_filter = LW_TARGET_ENEMIES;

    int dealt = lw_apply_attack_full(s, 0, s->entities[1].cell_id, &atk);
    /* d = 100 * 1.3 = 130. Erosion = round(130 * (0.05+0.10)) = round(19.5) = 20. */
    int ok = (dealt == 130 &&
              s->entities[1].hp == 870 &&
              s->entities[1].total_hp == 980);
    if (!ok) printf("  crit: dealt=%d hp=%d total=%d -> FAIL\n",
                    dealt, s->entities[1].hp, s->entities[1].total_hp);
    lw_state_free(s);
    return ok;
}


static int test_consumes_two_rng_draws(void) {
    /* Whether or not we hit anyone, the pipeline consumes exactly 2
     * draws (critical + jet). */
    build_15x15();
    LwState *s = fresh_two_entities(42);

    /* Aim at an empty cell so no targets. */
    LwAttackSpec atk = {0};
    atk.area = LW_AREA_TYPE_SINGLE_CELL;
    atk.min_range = 1; atk.max_range = 14;
    atk.launch_type = 1;
    atk.n_effects = 1;
    atk.effects[0].type = LW_EFFECT_DAMAGE;
    atk.effects[0].value1 = 100;
    atk.effects[0].value2 = 0;

    uint64_t before = s->rng_n;
    lw_apply_attack_full(s, 0, 5 * 15 + 5, &atk);  /* empty cell */
    uint64_t after = s->rng_n;

    /* Replay 2 draws on a parallel state seeded the same way. */
    uint64_t parallel = (uint64_t)(int64_t)42;
    lw_rng_double(&parallel);
    lw_rng_double(&parallel);
    int ok = (after == parallel);
    if (!ok) printf("  rng_draws: before=%llu after=%llu expected=%llu -> FAIL\n",
                    (unsigned long long)before, (unsigned long long)after,
                    (unsigned long long)parallel);
    lw_state_free(s);
    return ok;
}


static int test_kill_short_circuits(void) {
    /* KILL effect: target.hp -> 0 regardless of formula. */
    build_15x15();
    LwState *s = fresh_two_entities(42);

    LwAttackSpec atk = {0};
    atk.area = LW_AREA_TYPE_SINGLE_CELL;
    atk.min_range = 1; atk.max_range = 12;
    atk.launch_type = 1;
    atk.n_effects = 1;
    atk.effects[0].type = LW_EFFECT_KILL;
    atk.effects[0].targets_filter = LW_TARGET_ENEMIES;

    int dealt = lw_apply_attack_full(s, 0, s->entities[1].cell_id, &atk);
    int ok = (dealt == 1000 &&
              !s->entities[1].alive);
    if (!ok) printf("  kill: dealt=%d alive=%d -> FAIL\n",
                    dealt, s->entities[1].alive);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_attack_apply:\n");
    int n = 0, ok = 0;
    n++; if (test_simple_damage_attack())     { printf("   1  simple_damage OK\n"); ok++; }
    n++; if (test_buff_attack_registers_entry()) { printf("   2  buff_attack OK\n"); ok++; }
    n++; if (test_aoe_circle_hits_all())      { printf("   3  aoe_circle OK\n"); ok++; }
    n++; if (test_critical_doubles_damage())  { printf("   4  critical_doubles OK\n"); ok++; }
    n++; if (test_consumes_two_rng_draws())   { printf("   5  consumes_two_draws OK\n"); ok++; }
    n++; if (test_kill_short_circuits())      { printf("   6  kill_short_circuits OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
