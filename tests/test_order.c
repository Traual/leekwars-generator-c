/*
 * test_order.c -- start-order parity with Python's StartOrder.compute.
 *
 * Each case fixes (seed, team_id+frequency per entity) and the
 * expected initial_order. Reference outputs were captured from the
 * Python engine.
 */

#include "lw_order.h"
#include "lw_state.h"
#include <stdio.h>
#include <string.h>


static LwState* setup(int seed, int n, const int *team_ids,
                      const int *frequencies) {
    LwState *s = lw_state_alloc();
    s->n_entities = n;
    for (int i = 0; i < n; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i;
        s->entities[i].team_id = team_ids[i];
        s->entities[i].alive = 1;
        s->entities[i].cell_id = -1;
        s->entities[i].base_stats[LW_STAT_FREQUENCY] = frequencies[i];
    }
    s->rng_n = (uint64_t)(int64_t)seed;
    return s;
}


static int verify_order(LwState *s, const int *expected, int n,
                        const char *label) {
    if (s->n_in_order != n) {
        printf("  %s: n_in_order=%d expected=%d -> FAIL\n",
               label, s->n_in_order, n);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (s->initial_order[i] != expected[i]) {
            printf("  %s: order[%d]=%d expected=%d -> FAIL\n",
                   label, i, s->initial_order[i], expected[i]);
            for (int j = 0; j < n; j++) printf("  got[%d]=%d\n", j, s->initial_order[j]);
            return 0;
        }
    }
    return 1;
}


static int test_2v2_seed42(void) {
    int teams[]       = { 0,   0,   1,   1 };
    int frequencies[] = { 200, 100, 300, 150 };
    LwState *s = setup(42, 4, teams, frequencies);
    lw_compute_start_order(s);
    int expected[] = { 2, 0, 3, 1 };
    int ok = verify_order(s, expected, 4, "2v2_seed42");
    lw_state_free(s);
    return ok;
}


static int test_2v2_seed7(void) {
    int teams[]       = { 0,   0,   1,   1 };
    int frequencies[] = { 200, 100, 300, 150 };
    LwState *s = setup(7, 4, teams, frequencies);
    lw_compute_start_order(s);
    int expected[] = { 2, 0, 3, 1 };
    int ok = verify_order(s, expected, 4, "2v2_seed7");
    lw_state_free(s);
    return ok;
}


static int test_battle_royale_3_teams(void) {
    int teams[]       = { 0,   1,   2 };
    int frequencies[] = { 100, 200, 300 };
    LwState *s = setup(42, 3, teams, frequencies);
    lw_compute_start_order(s);
    int expected[] = { 2, 1, 0 };
    int ok = verify_order(s, expected, 3, "br_3_teams");
    lw_state_free(s);
    return ok;
}


static int test_solo_1v1(void) {
    int teams[]       = { 0,   1 };
    int frequencies[] = { 500, 100 };
    LwState *s = setup(42, 2, teams, frequencies);
    lw_compute_start_order(s);
    /* Python output: entities at indices 0 and 1 (original indices),
     * but with ids 5 and 7. The Python expected list is [5, 7] which
     * in our zero-indexed terms is [0, 1]. */
    int expected[] = { 0, 1 };
    int ok = verify_order(s, expected, 2, "1v1");
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_order:\n");
    int n = 0, ok = 0;
    n++; if (test_2v2_seed42())            { printf("   1  2v2_seed42 OK\n"); ok++; }
    n++; if (test_2v2_seed7())             { printf("   2  2v2_seed7 OK\n"); ok++; }
    n++; if (test_battle_royale_3_teams()) { printf("   3  battle_royale OK\n"); ok++; }
    n++; if (test_solo_1v1())              { printf("   4  solo_1v1 OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
