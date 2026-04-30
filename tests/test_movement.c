/*
 * test_movement.c -- Push / Attract / Slide / Teleport / Permutation.
 *
 * Built on a 15x15 grid with hand-placed entities. We verify:
 *   - push direction logic and step-by-step walk to first obstacle
 *   - attract is push with reversed direction
 *   - mismatched geometry returns entity unchanged
 *   - slide updates both entity.cell_id AND map.entity_at_cell
 *   - STATIC blocks slide but not teleport
 *   - permutation swaps cells atomically
 */

#include "lw_movement.h"
#include "lw_state.h"
#include <stdio.h>
#include <string.h>


static void build_15x15(LwTopology *topo) {
    memset(topo, 0, sizeof(*topo));
    topo->n_cells = 225;
    topo->min_x = 0; topo->max_x = 14;
    topo->min_y = 0; topo->max_y = 14;
    for (int y = 0; y < 15; y++)
        for (int x = 0; x < 15; x++) {
            int id = y * 15 + x;
            topo->cells[id].id = id;
            topo->cells[id].x = x;
            topo->cells[id].y = y;
            topo->cells[id].walkable = 1;
            topo->coord_lut[x][y] = id;
        }
    for (int x = 0; x < LW_COORD_DIM; x++)
        for (int y = 0; y < LW_COORD_DIM; y++)
            if (x >= 15 || y >= 15) topo->coord_lut[x][y] = -1;
}


static LwState* fresh_state_topo(LwTopology *topo) {
    LwState *s = lw_state_alloc();
    s->map.topo = topo;
    for (int i = 0; i < LW_MAX_CELLS; i++) s->map.entity_at_cell[i] = -1;
    return s;
}


static int test_push_basic(void) {
    /* Caster (3,3), entity at (5,3), target (10,3).
     * Direction caster->entity = (+1, 0); entity->target = (+1, 0) — same.
     * Entity should slide along row to (10, 3). */
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);
    s->n_entities = 2;
    memset(&s->entities[0], 0, sizeof(LwEntity));
    memset(&s->entities[1], 0, sizeof(LwEntity));
    s->entities[0].alive = 1; s->entities[0].cell_id = 3 * 15 + 3;
    s->entities[1].alive = 1; s->entities[1].cell_id = 3 * 15 + 5;
    s->map.entity_at_cell[s->entities[0].cell_id] = 0;
    s->map.entity_at_cell[s->entities[1].cell_id] = 1;

    int dest = lw_compute_push_dest(s, 3 * 15 + 5, 3 * 15 + 10, 3 * 15 + 3);
    int ok = (dest == 3 * 15 + 10);
    if (!ok) printf("  push_basic: dest=%d -> FAIL\n", dest);
    lw_state_free(s);
    return ok;
}


static int test_push_blocked_by_obstacle(void) {
    LwTopology topo;
    build_15x15(&topo);
    /* Block (3, 8). Push from (3, 5) toward (3, 10) -> stops at (3, 7). */
    topo.cells[3 * 15 + 8].walkable = 0;
    LwState *s = fresh_state_topo(&topo);

    int dest = lw_compute_push_dest(s, 3 * 15 + 5, 3 * 15 + 10, 3 * 15 + 3);
    int ok = (dest == 3 * 15 + 7);
    if (!ok) printf("  push_blocked: dest=%d expected %d -> FAIL\n",
                    dest, 3 * 15 + 7);
    lw_state_free(s);
    return ok;
}


static int test_push_blocked_by_entity(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);
    s->n_entities = 3;
    for (int i = 0; i < 3; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].alive = 1;
    }
    /* Caster (3,3), target (10,3), pushed entity at (5,3), blocker at (8,3). */
    s->entities[0].cell_id = 3 * 15 + 3;
    s->entities[1].cell_id = 3 * 15 + 5;  /* pushed */
    s->entities[2].cell_id = 3 * 15 + 8;  /* blocker */
    for (int i = 0; i < 3; i++)
        s->map.entity_at_cell[s->entities[i].cell_id] = i;

    int dest = lw_compute_push_dest(s, 3 * 15 + 5, 3 * 15 + 10, 3 * 15 + 3);
    int ok = (dest == 3 * 15 + 7);  /* Stops just before the blocker. */
    if (!ok) printf("  push_blocked_ent: dest=%d -> FAIL\n", dest);
    lw_state_free(s);
    return ok;
}


static int test_push_geometry_mismatch(void) {
    /* Caster at (3, 3), entity at (5, 3) (east of caster),
     * target at (5, 1) (north of entity, NOT east). cdx=+1, dx=0
     * mismatch -> push doesn't happen, entity stays put. */
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);

    int dest = lw_compute_push_dest(s, 3 * 15 + 5, 1 * 15 + 5, 3 * 15 + 3);
    int ok = (dest == 3 * 15 + 5);  /* unchanged */
    if (!ok) printf("  push_mismatch: dest=%d -> FAIL\n", dest);
    lw_state_free(s);
    return ok;
}


static int test_attract_basic(void) {
    /* Caster (3, 3), entity (3, 8), target (3, 5) — entity is east of
     * caster (cdx=0, cdy=+1), target is west of entity (dy=-1).
     * cdy=+1 == -dy=+1 -> match, so attract walks (3,8)->(3,7)->(3,6)->(3,5). */
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);

    int dest = lw_compute_attract_dest(s, 8 * 15 + 3, 5 * 15 + 3, 3 * 15 + 3);
    int ok = (dest == 5 * 15 + 3);
    if (!ok) printf("  attract_basic: dest=%d -> FAIL\n", dest);
    lw_state_free(s);
    return ok;
}


static int test_slide(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);
    s->n_entities = 1;
    memset(&s->entities[0], 0, sizeof(LwEntity));
    s->entities[0].alive = 1;
    s->entities[0].cell_id = 3 * 15 + 3;
    s->map.entity_at_cell[3 * 15 + 3] = 0;

    int rc = lw_apply_slide(s, 0, 5 * 15 + 5);
    int ok = (rc == 1 &&
              s->entities[0].cell_id == 5 * 15 + 5 &&
              s->map.entity_at_cell[5 * 15 + 5] == 0 &&
              s->map.entity_at_cell[3 * 15 + 3] == -1);
    if (!ok) printf("  slide: rc=%d -> FAIL\n", rc);
    lw_state_free(s);
    return ok;
}


static int test_slide_static_blocks(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);
    s->n_entities = 1;
    memset(&s->entities[0], 0, sizeof(LwEntity));
    s->entities[0].alive = 1;
    s->entities[0].cell_id = 3 * 15 + 3;
    s->entities[0].state_flags = LW_STATE_STATIC;
    s->map.entity_at_cell[3 * 15 + 3] = 0;

    int rc = lw_apply_slide(s, 0, 5 * 15 + 5);
    int ok = (rc == 0 && s->entities[0].cell_id == 3 * 15 + 3);
    if (!ok) printf("  slide_static: rc=%d -> FAIL\n", rc);
    lw_state_free(s);
    return ok;
}


static int test_teleport_bypasses_static(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);
    s->n_entities = 1;
    memset(&s->entities[0], 0, sizeof(LwEntity));
    s->entities[0].alive = 1;
    s->entities[0].cell_id = 3 * 15 + 3;
    s->entities[0].state_flags = LW_STATE_STATIC;
    s->map.entity_at_cell[3 * 15 + 3] = 0;

    int rc = lw_apply_teleport(s, 0, 5 * 15 + 5);
    int ok = (rc == 1 && s->entities[0].cell_id == 5 * 15 + 5);
    if (!ok) printf("  tp_static: rc=%d -> FAIL\n", rc);
    lw_state_free(s);
    return ok;
}


static int test_permutation_swap(void) {
    LwTopology topo;
    build_15x15(&topo);
    LwState *s = fresh_state_topo(&topo);
    s->n_entities = 2;
    for (int i = 0; i < 2; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].alive = 1;
    }
    int a = 3 * 15 + 3, b = 7 * 15 + 8;
    s->entities[0].cell_id = a; s->map.entity_at_cell[a] = 0;
    s->entities[1].cell_id = b; s->map.entity_at_cell[b] = 1;

    int rc = lw_apply_permutation(s, 0, 1);
    int ok = (rc == 1 &&
              s->entities[0].cell_id == b &&
              s->entities[1].cell_id == a &&
              s->map.entity_at_cell[b] == 0 &&
              s->map.entity_at_cell[a] == 1);
    if (!ok) printf("  perm: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_movement:\n");
    int n = 0, ok = 0;
    n++; if (test_push_basic())               { printf("   1  push_basic OK\n"); ok++; }
    n++; if (test_push_blocked_by_obstacle()) { printf("   2  push_blocked_obstacle OK\n"); ok++; }
    n++; if (test_push_blocked_by_entity())   { printf("   3  push_blocked_entity OK\n"); ok++; }
    n++; if (test_push_geometry_mismatch())   { printf("   4  push_mismatch OK\n"); ok++; }
    n++; if (test_attract_basic())            { printf("   5  attract_basic OK\n"); ok++; }
    n++; if (test_slide())                    { printf("   6  slide OK\n"); ok++; }
    n++; if (test_slide_static_blocks())      { printf("   7  slide_static_blocks OK\n"); ok++; }
    n++; if (test_teleport_bypasses_static()) { printf("   8  teleport_bypasses_static OK\n"); ok++; }
    n++; if (test_permutation_swap())         { printf("   9  permutation_swap OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
