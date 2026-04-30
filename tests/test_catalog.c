/*
 * test_catalog.c -- attack-spec registry + USE_WEAPON / USE_CHIP
 * routing through lw_apply_action.
 */

#include "lw_action.h"
#include "lw_attack_apply.h"
#include "lw_area.h"
#include "lw_catalog.h"
#include "lw_state.h"
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


static int test_register_lookup(void) {
    lw_catalog_clear();

    LwAttackSpec spec = {0};
    spec.attack_type = 1;
    spec.item_id = 42;
    spec.min_range = 1; spec.max_range = 8;
    spec.tp_cost = 5;
    spec.area = LW_AREA_TYPE_SINGLE_CELL;
    spec.n_effects = 1;
    spec.effects[0].type = LW_EFFECT_DAMAGE;
    spec.effects[0].value1 = 100;

    int rc = lw_catalog_register(42, &spec);
    int ok1 = (rc == 0 && lw_catalog_size() == 1);

    const LwAttackSpec *got = lw_catalog_get(42);
    int ok2 = (got != NULL && got->item_id == 42 && got->tp_cost == 5);

    int ok3 = (lw_catalog_get(99) == NULL);

    int ok = ok1 && ok2 && ok3;
    if (!ok) printf("  reg_lookup: %d %d %d -> FAIL\n", ok1, ok2, ok3);
    return ok;
}


static int test_overwrite(void) {
    lw_catalog_clear();
    LwAttackSpec a = {0};
    a.item_id = 1; a.tp_cost = 5;
    LwAttackSpec b = {0};
    b.item_id = 1; b.tp_cost = 7;
    lw_catalog_register(1, &a);
    lw_catalog_register(1, &b);
    int ok = (lw_catalog_size() == 1 &&
              lw_catalog_get(1)->tp_cost == 7);
    if (!ok) printf("  overwrite: -> FAIL\n");
    return ok;
}


static int test_use_weapon_via_catalog(void) {
    /* Register a weapon, build a state with caster + target, fire
     * USE_WEAPON via apply_action and verify the byte-for-byte
     * pipeline ran (target took damage, total_hp dropped from erosion). */
    build_15x15();
    lw_catalog_clear();

    LwAttackSpec spec = {0};
    spec.attack_type = 1;
    spec.item_id = 100;
    spec.min_range = 1; spec.max_range = 12;
    spec.launch_type = 1;
    spec.area = LW_AREA_TYPE_SINGLE_CELL;
    spec.tp_cost = 4;
    spec.n_effects = 1;
    spec.effects[0].type = LW_EFFECT_DAMAGE;
    spec.effects[0].value1 = 100;
    spec.effects[0].value2 = 0;
    spec.effects[0].targets_filter = LW_TARGET_ENEMIES;
    lw_catalog_register(100, &spec);

    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;
    s->n_entities = 2;
    LwEntity *c = &s->entities[0];
    LwEntity *t = &s->entities[1];
    memset(c, 0, sizeof(*c));
    memset(t, 0, sizeof(*t));
    c->id = 0; c->team_id = 0; c->alive = 1; c->cell_id = 7 * 15 + 3;
    c->hp = 1000; c->total_hp = 1000;
    c->base_stats[LW_STAT_TP] = 20;
    c->n_weapons = 1; c->weapons[0] = 100; c->equipped_weapon = 0;
    s->map.entity_at_cell[c->cell_id] = 0;

    t->id = 1; t->team_id = 1; t->alive = 1; t->cell_id = 7 * 15 + 10;
    t->hp = 1000; t->total_hp = 1000;
    s->map.entity_at_cell[t->cell_id] = 1;
    s->rng_n = 42;

    LwAction a = {0};
    a.type = LW_ACTION_USE_WEAPON;
    a.weapon_id = 100;
    a.target_cell_id = t->cell_id;

    int rc = lw_apply_action(s, 0, &a);
    int ok = (rc == 1 &&
              s->entities[1].hp < 1000 &&
              s->entities[1].total_hp < 1000 &&  /* erosion */
              s->entities[0].used_tp == 4);
    if (!ok) printf("  use_weapon_cat: rc=%d hp=%d total=%d tp=%d -> FAIL\n",
                    rc, s->entities[1].hp, s->entities[1].total_hp,
                    s->entities[0].used_tp);
    lw_state_free(s);
    return ok;
}


static int test_use_weapon_falls_back_when_no_catalog(void) {
    build_15x15();
    lw_catalog_clear();

    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;
    s->n_entities = 2;
    LwEntity *c = &s->entities[0];
    LwEntity *t = &s->entities[1];
    memset(c, 0, sizeof(*c));
    memset(t, 0, sizeof(*t));
    c->id = 0; c->team_id = 0; c->alive = 1; c->cell_id = 7 * 15 + 3;
    c->hp = 1000; c->total_hp = 1000;
    c->base_stats[LW_STAT_TP] = 20;
    c->n_weapons = 1; c->weapons[0] = 999; c->equipped_weapon = 0;
    s->map.entity_at_cell[c->cell_id] = 0;
    t->id = 1; t->team_id = 1; t->alive = 1; t->cell_id = 7 * 15 + 10;
    t->hp = 1000; t->total_hp = 1000;
    s->map.entity_at_cell[t->cell_id] = 1;

    LwAction a = {0};
    a.type = LW_ACTION_USE_WEAPON;
    a.weapon_id = 999;  /* unknown to catalog */
    a.target_cell_id = t->cell_id;

    int rc = lw_apply_action(s, 0, &a);
    int ok = (rc == 1 &&
              s->entities[1].hp < 1000 &&
              /* Stub doesn't apply erosion, total_hp unchanged. */
              s->entities[1].total_hp == 1000);
    if (!ok) printf("  fallback: rc=%d hp=%d total=%d -> FAIL\n",
                    rc, s->entities[1].hp, s->entities[1].total_hp);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_catalog:\n");
    int n = 0, ok = 0;
    n++; if (test_register_lookup())                  { printf("   1  register_lookup OK\n"); ok++; }
    n++; if (test_overwrite())                        { printf("   2  overwrite OK\n"); ok++; }
    n++; if (test_use_weapon_via_catalog())           { printf("   3  use_weapon_via_catalog OK\n"); ok++; }
    n++; if (test_use_weapon_falls_back_when_no_catalog()) { printf("   4  use_weapon_fallback OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
