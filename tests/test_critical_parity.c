/*
 * test_critical_parity.c -- byte-for-byte parity for the critical roll.
 *
 * Reference outputs were captured from the Python engine's
 * _DefaultRandom seeded with seed=42, then compared against
 * threshold = agility/1000.0. The C engine must produce the SAME
 * sequence of critical/non-critical decisions for every (seed, agility)
 * pair we test.
 *
 * Three regimes:
 *   1. Agility = 0      -> always false (threshold 0.0)
 *   2. Agility >= 1000  -> always true  (threshold >= 1.0)
 *   3. Agility = 300/750 -> use captured sequences
 */

#include "lw_critical.h"
#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


static LwState* fresh_state_seeded(int seed, int agility) {
    LwState *s = lw_state_alloc();
    s->n_entities = 1;
    LwEntity *c = &s->entities[0];
    memset(c, 0, sizeof(*c));
    c->id = 0; c->fid = 0; c->team_id = 0; c->alive = 1; c->cell_id = -1;
    c->hp = 1000; c->total_hp = 1000;
    c->base_stats[LW_STAT_AGILITY] = agility;
    s->rng_n = (uint64_t)(int64_t)seed;
    s->seed = seed;
    return s;
}


static int test_no_agility_never_crits(void) {
    LwState *s = fresh_state_seeded(42, 0);
    int crits = 0;
    for (int i = 0; i < 1000; i++) {
        crits += lw_roll_critical(s, 0);
    }
    int ok = (crits == 0);
    if (!ok) printf("  no_agility: %d crits in 1000 rolls -> FAIL\n", crits);
    lw_state_free(s);
    return ok;
}


static int test_full_agility_always_crits(void) {
    /* agility=1000 -> threshold=1.0; get_double() < 1.0 always. */
    LwState *s = fresh_state_seeded(42, 1000);
    int crits = 0;
    for (int i = 0; i < 1000; i++) {
        crits += lw_roll_critical(s, 0);
    }
    int ok = (crits == 1000);
    if (!ok) printf("  full_agility: %d crits in 1000 rolls -> FAIL\n", crits);
    lw_state_free(s);
    return ok;
}


static int test_agility_300_seed_42(void) {
    /* Captured from Python:
     *   _DefaultRandom seeded 42, threshold 0.300, first 20 rolls:
     *   F T F F T F F F F F F F F T F T F T F F
     */
    int expected[20] = {
        0, 1, 0, 0, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 1, 0, 1, 0, 1, 0, 0
    };
    LwState *s = fresh_state_seeded(42, 300);
    int ok = 1;
    for (int i = 0; i < 20; i++) {
        int got = lw_roll_critical(s, 0);
        if (got != expected[i]) {
            printf("  ag300_seed42[%d]: got=%d expected=%d -> FAIL\n",
                   i, got, expected[i]);
            ok = 0;
        }
    }
    lw_state_free(s);
    return ok;
}


static int test_agility_750_seed_42(void) {
    /* Captured from Python:
     *   _DefaultRandom seeded 42, threshold 0.750, first 20 rolls:
     *   F T T F T T T T T F T F F T F T F T T T
     */
    int expected[20] = {
        0, 1, 1, 0, 1, 1, 1, 1, 1, 0,
        1, 0, 0, 1, 0, 1, 0, 1, 1, 1
    };
    LwState *s = fresh_state_seeded(42, 750);
    int ok = 1;
    for (int i = 0; i < 20; i++) {
        int got = lw_roll_critical(s, 0);
        if (got != expected[i]) {
            printf("  ag750_seed42[%d]: got=%d expected=%d -> FAIL\n",
                   i, got, expected[i]);
            ok = 0;
        }
    }
    lw_state_free(s);
    return ok;
}


static int test_critical_power_factor(void) {
    /* Crit produces 1.3, no-crit produces 1.0. */
    LwState *s = fresh_state_seeded(42, 300);
    /* First crit at index 1 in the agility=300/seed=42 sequence. */
    double p0 = lw_roll_critical_power(s, 0);
    double p1 = lw_roll_critical_power(s, 0);
    int ok = (p0 == 1.0 && p1 == LW_CRITICAL_FACTOR);
    if (!ok) printf("  crit_power: p0=%g p1=%g expected 1.0 1.3 -> FAIL\n",
                    p0, p1);
    lw_state_free(s);
    return ok;
}


static int test_buff_agility_counted(void) {
    /* Critical roll uses base+buff agility. With base=200, buff=600,
     * effective threshold is 800/1000=0.8 — first 5 rolls of seed=42
     * are 0.79, 0.26, 0.73, 0.89, 0.21 so crits = T T T F T. */
    int expected[5] = { 1, 1, 1, 0, 1 };
    LwState *s = fresh_state_seeded(42, 200);
    s->entities[0].buff_stats[LW_STAT_AGILITY] = 600;
    int ok = 1;
    for (int i = 0; i < 5; i++) {
        int got = lw_roll_critical(s, 0);
        if (got != expected[i]) {
            printf("  buff_ag[%d]: got=%d expected=%d -> FAIL\n",
                   i, got, expected[i]);
            ok = 0;
        }
    }
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_critical_parity:\n");
    int n = 0, ok = 0;
    n++; if (test_no_agility_never_crits())     { printf("   1  no_agility OK\n"); ok++; }
    n++; if (test_full_agility_always_crits())  { printf("   2  full_agility OK\n"); ok++; }
    n++; if (test_agility_300_seed_42())        { printf("   3  agility=300 seq OK\n"); ok++; }
    n++; if (test_agility_750_seed_42())        { printf("   4  agility=750 seq OK\n"); ok++; }
    n++; if (test_critical_power_factor())      { printf("   5  critical_power OK\n"); ok++; }
    n++; if (test_buff_agility_counted())       { printf("   6  buff_agility OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
