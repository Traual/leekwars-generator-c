/*
 * test_action_stream.c -- verify the action log emits the right
 * entries when an attack pipeline runs.
 */

#include "lw_action_stream.h"
#include "lw_attack_apply.h"
#include "lw_area.h"
#include "lw_catalog.h"
#include "lw_state.h"
#include <stdio.h>
#include <string.h>


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


static int test_emit_disabled_by_default(void) {
    LwState *s = lw_state_alloc();
    /* state.stream.enabled defaults to 0; emits should be no-ops. */
    int n_before = s->stream.n;
    lw_action_emit(s, LW_ACT_DAMAGE, 0, 1, 100, 5, 0);
    int n_after = s->stream.n;
    int ok = (n_before == 0 && n_after == 0);
    if (!ok) printf("  default_disabled: n_before=%d n_after=%d -> FAIL\n",
                    n_before, n_after);
    lw_state_free(s);
    return ok;
}


static int test_emit_when_enabled(void) {
    LwState *s = lw_state_alloc();
    s->stream.enabled = 1;
    lw_action_emit(s, LW_ACT_DAMAGE, 0, 1, 100, 5, 7);
    int ok = (s->stream.n == 1 &&
              s->stream.entries[0].type == LW_ACT_DAMAGE &&
              s->stream.entries[0].caster_id == 0 &&
              s->stream.entries[0].target_id == 1 &&
              s->stream.entries[0].value1 == 100 &&
              s->stream.entries[0].value2 == 5 &&
              s->stream.entries[0].value3 == 7);
    if (!ok) printf("  emit_enabled: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


static int test_attack_emits_use_weapon_and_damage(void) {
    build_15x15();
    lw_catalog_clear();

    LwAttackSpec gun = {0};
    gun.attack_type = 1; gun.item_id = 42;
    gun.min_range = 1; gun.max_range = 12;
    gun.launch_type = 1; gun.area = LW_AREA_TYPE_SINGLE_CELL;
    gun.tp_cost = 4;
    gun.n_effects = 1;
    gun.effects[0].type = LW_EFFECT_DAMAGE;
    gun.effects[0].value1 = 100;
    gun.effects[0].value2 = 50;
    gun.effects[0].targets_filter = LW_TARGET_ENEMIES;
    lw_catalog_register(42, &gun);

    LwState *s = lw_state_alloc();
    s->map.topo = &g_topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;
    s->n_entities = 2;
    for (int i = 0; i < 2; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i;
        s->entities[i].alive = 1;
        s->entities[i].hp = 1000; s->entities[i].total_hp = 1000;
        s->entities[i].base_stats[LW_STAT_STRENGTH] = 100;
    }
    s->entities[0].team_id = 0;
    s->entities[0].cell_id = 7 * 15 + 3;
    s->map.entity_at_cell[s->entities[0].cell_id] = 0;
    s->entities[1].team_id = 1;
    s->entities[1].cell_id = 7 * 15 + 10;
    s->map.entity_at_cell[s->entities[1].cell_id] = 1;
    s->rng_n = 42;
    s->stream.enabled = 1;

    lw_apply_attack_full(s, 0, s->entities[1].cell_id, &gun);

    /* Expected log: USE_WEAPON, then DAMAGE. */
    int ok = (s->stream.n >= 2 &&
              s->stream.entries[0].type == LW_ACT_USE_WEAPON &&
              s->stream.entries[0].caster_id == 0 &&
              s->stream.entries[0].value2 == 42 /* item_id */ &&
              s->stream.entries[1].type == LW_ACT_DAMAGE &&
              s->stream.entries[1].caster_id == 0 &&
              s->stream.entries[1].target_id == 1 &&
              s->stream.entries[1].value1 > 0);
    if (!ok) {
        printf("  attack_emits: n=%d", s->stream.n);
        for (int i = 0; i < s->stream.n && i < 4; i++)
            printf("  [%d] type=%d caster=%d target=%d v=%d/%d/%d",
                   i, s->stream.entries[i].type,
                   s->stream.entries[i].caster_id,
                   s->stream.entries[i].target_id,
                   s->stream.entries[i].value1,
                   s->stream.entries[i].value2,
                   s->stream.entries[i].value3);
        printf(" -> FAIL\n");
    }
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_action_stream:\n");
    int n = 0, ok = 0;
    n++; if (test_emit_disabled_by_default())          { printf("   1  default_disabled OK\n"); ok++; }
    n++; if (test_emit_when_enabled())                 { printf("   2  emit_when_enabled OK\n"); ok++; }
    n++; if (test_attack_emits_use_weapon_and_damage()){ printf("   3  attack_emits_log OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
