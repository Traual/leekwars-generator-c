/*
 * test_effects_parity.c -- buff + poison effect ports.
 *
 * Each case fixes (effect_kind, value1, value2, jet, caster stats,
 * target stats, aoe, critical_power) and the expected outcome.
 * Outcomes were derived directly from the Python apply formulas.
 */

#include "lw_damage.h"
#include "lw_effects.h"
#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


static LwState* fresh_state(void) {
    LwState *s = lw_state_alloc();
    s->n_entities = 2;
    LwEntity *c = &s->entities[0];
    LwEntity *t = &s->entities[1];
    memset(c, 0, sizeof(*c));
    memset(t, 0, sizeof(*t));
    c->id = 0; c->fid = 0; c->team_id = 0; c->alive = 1; c->cell_id = -1;
    t->id = 1; t->fid = 1; t->team_id = 1; t->alive = 1; t->cell_id = -1;
    t->hp = 1000; t->total_hp = 1000;
    return s;
}


static int test_heal(void) {
    /* (v1=50, v2=50, jet=0.5, wisdom=100, aoe=1, crit=1, tc=1)
     * v = (50 + 0.5*50)(1 + 100/100)*1*1*1 = 75 * 2 = 150
     * target HP 1000, missing 0 if at full. Make missing 200.
     */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_WISDOM] = 100;
    s->entities[1].hp = 800;  /* missing 200 */
    int healed = lw_apply_heal(s, 0, 1, 50, 50, 0.5, 1.0, 1.0, 1);
    int ok = (healed == 150 && s->entities[1].hp == 950);
    if (!ok) printf("  heal: healed=%d hp=%d expected 150 950 -> FAIL\n",
                    healed, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_heal_capped(void) {
    /* Healing capped to missing HP. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_WISDOM] = 0;
    s->entities[1].hp = 990;  /* missing 10 */
    int healed = lw_apply_heal(s, 0, 1, 50, 50, 0.5, 1.0, 1.0, 1);
    int ok = (healed == 10 && s->entities[1].hp == 1000);
    if (!ok) printf("  heal_capped: healed=%d hp=%d expected 10 1000 -> FAIL\n",
                    healed, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_heal_unhealable(void) {
    LwState *s = fresh_state();
    s->entities[1].hp = 500;
    s->entities[1].state_flags |= LW_STATE_UNHEALABLE;
    int healed = lw_apply_heal(s, 0, 1, 50, 50, 0.5, 1.0, 1.0, 1);
    int ok = (healed == 0 && s->entities[1].hp == 500);
    if (!ok) printf("  heal_unhealable: healed=%d expected 0 -> FAIL\n", healed);
    lw_state_free(s);
    return ok;
}


static int test_abs_shield(void) {
    /* (v1=20, v2=20, jet=0.5, resistance=100, aoe=1, crit=1)
     * v = (20 + 0.5*20)(1 + 100/100)*1*1 = 30 * 2 = 60.
     */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_RESISTANCE] = 100;
    int g = lw_apply_absolute_shield(s, 0, 1, 20, 20, 0.5, 1.0, 1.0);
    int ok = (g == 60 &&
              s->entities[1].buff_stats[LW_STAT_ABSOLUTE_SHIELD] == 60);
    if (!ok) printf("  abs_shield: granted=%d buff=%d expected 60 -> FAIL\n",
                    g, s->entities[1].buff_stats[LW_STAT_ABSOLUTE_SHIELD]);
    lw_state_free(s);
    return ok;
}


static int test_rel_shield(void) {
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_RESISTANCE] = 0;
    int g = lw_apply_relative_shield(s, 0, 1, 10, 10, 0.5, 1.0, 1.0);
    int ok = (g == 15 &&
              s->entities[1].buff_stats[LW_STAT_RELATIVE_SHIELD] == 15);
    if (!ok) printf("  rel_shield: granted=%d buff=%d expected 15 -> FAIL\n",
                    g, s->entities[1].buff_stats[LW_STAT_RELATIVE_SHIELD]);
    lw_state_free(s);
    return ok;
}


static int test_buff_strength(void) {
    /* (v1=30, v2=30, jet=0.5, science=100, aoe=1, crit=1)
     * v = (30 + 30*0.5)(1 + 1) * 1 * 1 = 45 * 2 = 90.
     */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_SCIENCE] = 100;
    int amt = lw_apply_buff_stat(s, 0, 1, LW_STAT_STRENGTH,
                                  30, 30, 0.5, 1.0, 1.0);
    int ok = (amt == 90 &&
              s->entities[1].buff_stats[LW_STAT_STRENGTH] == 90);
    if (!ok) printf("  buff_strength: amt=%d buff=%d expected 90 -> FAIL\n",
                    amt, s->entities[1].buff_stats[LW_STAT_STRENGTH]);
    lw_state_free(s);
    return ok;
}


static int test_poison_compute_and_tick(void) {
    /* poison damage = (v1 + jet*v2)(1 + magic/100)*aoe*crit*(1+power/100)
     * v1=20, v2=10, jet=0.5, magic=100, power=0, aoe=1, crit=1
     * = 25 * 2 * 1 * 1 = 50 per turn.
     */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_MAGIC] = 100;
    int per_turn = lw_compute_poison_damage(s, 0, 1, 20, 10, 0.5, 1.0, 1.0);
    int ok1 = (per_turn == 50);

    /* tick: target loses 50 HP per call. */
    int dealt1 = lw_tick_poison(s, 1, per_turn);
    int dealt2 = lw_tick_poison(s, 1, per_turn);
    int ok2 = (dealt1 == 50 && dealt2 == 50 && s->entities[1].hp == 900);

    /* tick respects HP cap. */
    s->entities[1].hp = 30;
    int dealt3 = lw_tick_poison(s, 1, per_turn);
    int ok3 = (dealt3 == 30 && !s->entities[1].alive);

    int ok = ok1 && ok2 && ok3;
    if (!ok) printf("  poison: per_turn=%d ok1=%d ok2=%d ok3=%d -> FAIL\n",
                    per_turn, ok1, ok2, ok3);
    lw_state_free(s);
    return ok;
}


static int test_poison_invincible(void) {
    LwState *s = fresh_state();
    s->entities[1].state_flags |= LW_STATE_INVINCIBLE;
    int dealt = lw_tick_poison(s, 1, 100);
    int ok = (dealt == 0 && s->entities[1].hp == 1000);
    if (!ok) printf("  poison_invincible: dealt=%d expected 0 -> FAIL\n", dealt);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_effects_parity:\n");
    int n = 0, ok = 0;
    n++; if (test_heal())                   { printf("   1  heal OK\n"); ok++; }
    n++; if (test_heal_capped())            { printf("   2  heal_capped OK\n"); ok++; }
    n++; if (test_heal_unhealable())        { printf("   3  heal_unhealable OK\n"); ok++; }
    n++; if (test_abs_shield())             { printf("   4  abs_shield OK\n"); ok++; }
    n++; if (test_rel_shield())             { printf("   5  rel_shield OK\n"); ok++; }
    n++; if (test_buff_strength())          { printf("   6  buff_strength OK\n"); ok++; }
    n++; if (test_poison_compute_and_tick()){ printf("   7  poison OK\n"); ok++; }
    n++; if (test_poison_invincible())      { printf("   8  poison_invincible OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
