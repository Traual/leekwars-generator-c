/*
 * test_erosion.c -- max-HP erosion (Entity.removeLife's erosion arg).
 *
 * Erosion is the lesser-known half of Python's removeLife: the
 * second arg ``erosion`` reduces target.total_hp (with a floor of 1).
 * For damage effects:
 *   erosion = round(dealt * rate)
 *   rate = EROSION_DAMAGE (0.05) for direct/aftereffect damage
 *        | EROSION_POISON (0.10) for poison
 *   plus EROSION_CRITICAL_BONUS (0.10) on a crit.
 */

#include "lw_damage.h"
#include "lw_state.h"
#include "lw_effect.h"  /* for LW_EFFECT_POISON */
#include <assert.h>
#include <stdio.h>
#include <string.h>


static LwState* fresh_state(void) {
    LwState *s = lw_state_alloc();
    s->n_entities = 1;
    LwEntity *t = &s->entities[0];
    memset(t, 0, sizeof(*t));
    t->id = 0; t->alive = 1; t->cell_id = -1;
    t->hp = 1000; t->total_hp = 1000;
    return s;
}


static int test_basic(void) {
    /* dealt=100, rate=0.05 -> 5 max-HP off. */
    LwState *s = fresh_state();
    int e = lw_apply_erosion(s, 0, 100, 0.05);
    int ok = (e == 5 && s->entities[0].total_hp == 995);
    if (!ok) printf("  basic: e=%d total=%d -> FAIL\n",
                    e, s->entities[0].total_hp);
    lw_state_free(s);
    return ok;
}


static int test_zero_rate(void) {
    LwState *s = fresh_state();
    int e = lw_apply_erosion(s, 0, 100, 0.0);
    int ok = (e == 0 && s->entities[0].total_hp == 1000);
    if (!ok) printf("  zero_rate: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


static int test_zero_value(void) {
    LwState *s = fresh_state();
    int e = lw_apply_erosion(s, 0, 0, 0.05);
    int ok = (e == 0 && s->entities[0].total_hp == 1000);
    if (!ok) printf("  zero_value: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


static int test_floors_at_one(void) {
    /* Even huge erosion can't push total_hp below 1. */
    LwState *s = fresh_state();
    s->entities[0].total_hp = 50;
    int e = lw_apply_erosion(s, 0, 10000, 0.50);
    /* erosion = round(5000) = 5000; total_hp = 50 - 5000 = clamped to 1. */
    int ok = (e == 5000 && s->entities[0].total_hp == 1);
    if (!ok) printf("  floors: e=%d total=%d -> FAIL\n",
                    e, s->entities[0].total_hp);
    lw_state_free(s);
    return ok;
}


static int test_rounding(void) {
    /* dealt=33, rate=0.10 -> round(3.3) = 3. */
    LwState *s = fresh_state();
    int e = lw_apply_erosion(s, 0, 33, 0.10);
    int ok = (e == 3 && s->entities[0].total_hp == 997);
    if (!ok) printf("  rounding: e=%d -> FAIL\n", e);
    lw_state_free(s);
    return ok;
}


static int test_rate_helper(void) {
    /* DAMAGE no crit -> 0.05; with crit -> 0.05 + 0.10.
     * POISON no crit -> 0.10; with crit -> 0.10 + 0.10.
     * Compare with abs(diff) < 1e-12 to avoid FP literal mismatch
     * (0.05 + 0.10 != 0.15 exactly in IEEE 754). */
    int ok = 1;
    double r;
    double tol = 1e-12;
#define APPROX_EQ(a, b) (((a) - (b) > -tol) && ((a) - (b) < tol))

    r = lw_erosion_rate(1 /*DAMAGE*/, 0);
    if (!APPROX_EQ(r, 0.05)) { printf("  rate dmg: %g\n", r); ok = 0; }

    r = lw_erosion_rate(1 /*DAMAGE*/, 1);
    if (!APPROX_EQ(r, 0.05 + 0.10)) { printf("  rate dmg+crit: %g\n", r); ok = 0; }

    r = lw_erosion_rate(LW_EFFECT_POISON, 0);
    if (!APPROX_EQ(r, 0.10)) { printf("  rate poison: %g\n", r); ok = 0; }

    r = lw_erosion_rate(LW_EFFECT_POISON, 1);
    if (!APPROX_EQ(r, 0.10 + 0.10)) { printf("  rate poison+crit: %g\n", r); ok = 0; }

#undef APPROX_EQ
    if (!ok) printf("  rate_helper -> FAIL\n");
    return ok;
}


int main(void) {
    printf("test_erosion:\n");
    int n = 0, ok = 0;
    n++; if (test_basic())          { printf("   1  basic OK\n"); ok++; }
    n++; if (test_zero_rate())      { printf("   2  zero_rate OK\n"); ok++; }
    n++; if (test_zero_value())     { printf("   3  zero_value OK\n"); ok++; }
    n++; if (test_floors_at_one())  { printf("   4  floors_at_one OK\n"); ok++; }
    n++; if (test_rounding())       { printf("   5  rounding OK\n"); ok++; }
    n++; if (test_rate_helper())    { printf("   6  rate_helper OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
