/*
 * test_damage_parity.c -- assert lw_apply_damage matches Python's
 * EffectDamage.apply byte-for-byte on a panel of synthetic cases.
 *
 * Each case fixes (value1, value2, jet, strength, power, aoe,
 * critical_power, target_count, rel_shield, abs_shield, target_hp,
 * invincible) and the expected dealt damage. Values come from running
 * the Python EffectDamage.apply against the same inputs (no engine
 * scaffolding -- pure formula).
 *
 * Once these all pass we know the C damage routine is bit-exact for
 * single-target damage. Effects that wrap damage (poison decay,
 * return damage, life steal) are tested separately when ported.
 */

#include "lw_damage.h"
#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


typedef struct {
    const char *name;
    double v1, v2, jet;
    int    strength, power;
    double aoe, crit;
    int    target_count;
    int    rel_shield, abs_shield;
    int    target_hp;
    int    invincible;
    int    expected_dealt;
} Case;

/*
 * NB: Python's formula is ``d = value1 + jet * value2`` -- so jet=0.5
 * with v1=80, v2=120 yields 80 + 60 = 140, NOT (v1+v2)/2 = 100.
 * value2 acts as the *variance* term, not the max.
 * Expected values below were derived directly from the formula.
 */
static const Case CASES[] = {
    /* base = 80 + 0.5*120 = 140; no mods -> 140 */
    { "plain",      80, 120, 0.5,    0, 0, 1.0, 1.0, 1, 0,   0, 1000, 0, 140 },
    /* strength 100 -> *2 -> 280 */
    { "strength",   80, 120, 0.5,  100, 0, 1.0, 1.0, 1, 0,   0, 1000, 0, 280 },
    /* crit power 2 -> *2 -> 280 */
    { "critical",   80, 120, 0.5,    0, 0, 1.0, 2.0, 1, 0,   0, 1000, 0, 280 },
    /* rel shield 50% -> 140 - 70 = 70 */
    { "rel_shield", 80, 120, 0.5,    0, 0, 1.0, 1.0, 1, 50,  0, 1000, 0,  70 },
    /* abs shield 30 -> 140 - 30 = 110 */
    { "abs_shield", 80, 120, 0.5,    0, 0, 1.0, 1.0, 1, 0,  30, 1000, 0, 110 },
    /* rel 50% then abs 30 -> 70 - 30 = 40 */
    { "both_shield",80, 120, 0.5,    0, 0, 1.0, 1.0, 1, 50, 30, 1000, 0,  40 },
    /* HP cap: target HP 50 < 140 -> dealt 50 */
    { "hp_cap",     80, 120, 0.5,    0, 0, 1.0, 1.0, 1, 0,   0,   50, 0,  50 },
    /* INVINCIBLE -> 0 regardless */
    { "invincible", 80, 120, 0.5,    0, 0, 1.0, 1.0, 1, 0,   0, 1000, 1,   0 },
    /* aoe 0.5 -> 70 */
    { "aoe_half",   80, 120, 0.5,    0, 0, 0.5, 1.0, 1, 0,   0, 1000, 0,  70 },
    /* target_count 3 -> 420 */
    { "tc_3",       80, 120, 0.5,    0, 0, 1.0, 1.0, 3, 0,   0, 1000, 0, 420 },
    /* power 50 -> *1.5 -> 210 */
    { "power",      80, 120, 0.5,    0, 50, 1.0, 1.0, 1, 0,  0, 1000, 0, 210 },
    /* str 100 + power 50 + crit 2 -> 140*2*2*1.5 = 840 */
    { "combo",      80, 120, 0.5,  100, 50, 1.0, 2.0, 1, 0,  0, 1000, 0, 840 },
    /* jet 0 -> 80 */
    { "jet0",       80, 120, 0.0,    0, 0, 1.0, 1.0, 1, 0,  0, 1000, 0,  80 },
    /* jet 1 -> 200 */
    { "jet1",       80, 120, 1.0,    0, 0, 1.0, 1.0, 1, 0,  0, 1000, 0, 200 },
    /* negative strength clipped to 0 -> 140 (no bonus) */
    { "neg_str",    80, 120, 0.5,  -50, 0, 1.0, 1.0, 1, 0,  0, 1000, 0, 140 },
    /* over-shield: abs 200 absorbs all of 140 -> 0 */
    { "over_shield", 80, 120, 0.5,    0, 0, 1.0, 1.0, 1, 0, 200, 1000, 0,  0 },
};

static const int N_CASES = sizeof(CASES) / sizeof(CASES[0]);


static int run_case(const Case *c) {
    LwState *s = lw_state_alloc();
    s->n_entities = 2;
    LwEntity *caster = &s->entities[0];
    LwEntity *target = &s->entities[1];

    memset(caster, 0, sizeof(*caster));
    memset(target, 0, sizeof(*target));
    caster->id = 0; caster->fid = 0; caster->team_id = 0; caster->alive = 1;
    target->id = 1; target->fid = 1; target->team_id = 1; target->alive = 1;
    caster->base_stats[LW_STAT_STRENGTH] = c->strength;
    caster->base_stats[LW_STAT_POWER]    = c->power;
    target->base_stats[LW_STAT_RELATIVE_SHIELD] = c->rel_shield;
    target->base_stats[LW_STAT_ABSOLUTE_SHIELD] = c->abs_shield;
    target->hp = c->target_hp;
    target->total_hp = 1000;
    target->cell_id = -1;
    if (c->invincible) target->state_flags |= LW_STATE_INVINCIBLE;

    int dealt = lw_apply_damage(s, 0, 1, c->v1, c->v2, c->jet, c->aoe,
                                c->crit, c->target_count);

    int ok = (dealt == c->expected_dealt);
    if (!ok) {
        printf("  case '%s': dealt=%d expected=%d -> FAIL\n",
               c->name, dealt, c->expected_dealt);
    }
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_damage_parity:\n");
    int n_ok = 0;
    for (int i = 0; i < N_CASES; i++) {
        int ok = run_case(&CASES[i]);
        if (ok) {
            printf("  %2d/%2d  '%s' OK\n", i + 1, N_CASES, CASES[i].name);
            n_ok++;
        }
    }
    printf("\n%d/%d cases passed\n", n_ok, N_CASES);
    return (n_ok == N_CASES) ? 0 : 1;
}
