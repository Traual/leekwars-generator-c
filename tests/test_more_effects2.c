/*
 * test_more_effects2.c -- raw buffs, vulnerabilities, kill, add_state,
 * steal_absolute_shield, remove_shackles.
 */

#include "lw_effects.h"
#include "lw_effect_store.h"
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
    c->hp = 1000; c->total_hp = 1000;
    t->id = 1; t->fid = 1; t->team_id = 1; t->alive = 1; t->cell_id = -1;
    t->hp = 1000; t->total_hp = 1000;
    return s;
}


static int test_raw_buff_strength(void) {
    /* No scaling: v = (40 + 0.5*40) * 1 * 1 = 60. */
    LwState *s = fresh_state();
    int amt = lw_apply_raw_buff_stat(s, 1, LW_STAT_STRENGTH,
                                      40, 40, 0.5, 1.0, 1.0);
    int ok = (amt == 60 &&
              s->entities[1].buff_stats[LW_STAT_STRENGTH] == 60);
    if (!ok) printf("  raw_buff: amt=%d -> FAIL\n", amt);
    lw_state_free(s);
    return ok;
}


static int test_raw_buff_ignores_caster_stat(void) {
    /* Even with science=200 on the caster, the raw buff should be the
     * same value as without it (no scaling applied). */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_SCIENCE] = 200;
    int amt = lw_apply_raw_buff_stat(s, 1, LW_STAT_AGILITY,
                                      40, 40, 0.5, 1.0, 1.0);
    int ok = (amt == 60 &&
              s->entities[1].buff_stats[LW_STAT_AGILITY] == 60);
    if (!ok) printf("  raw_buff_ignores: amt=%d -> FAIL\n", amt);
    lw_state_free(s);
    return ok;
}


static int test_vulnerability(void) {
    /* v=(20+10)*1*1 = 30 -> -30 to relative shield slot. */
    LwState *s = fresh_state();
    int amt = lw_apply_vulnerability(s, 1, 20, 20, 0.5, 1.0, 1.0);
    int ok = (amt == 30 &&
              s->entities[1].buff_stats[LW_STAT_RELATIVE_SHIELD] == -30);
    if (!ok) printf("  vulnerability: amt=%d buff=%d -> FAIL\n",
                    amt, s->entities[1].buff_stats[LW_STAT_RELATIVE_SHIELD]);
    lw_state_free(s);
    return ok;
}


static int test_absolute_vulnerability(void) {
    LwState *s = fresh_state();
    int amt = lw_apply_absolute_vulnerability(s, 1, 20, 20, 0.5, 1.0, 1.0);
    int ok = (amt == 30 &&
              s->entities[1].buff_stats[LW_STAT_ABSOLUTE_SHIELD] == -30);
    if (!ok) printf("  abs_vulnerability: amt=%d -> FAIL\n", amt);
    lw_state_free(s);
    return ok;
}


static int test_kill(void) {
    LwState *s = fresh_state();
    int lost = lw_apply_kill(s, 0, 1);
    int ok = (lost == 1000 &&
              s->entities[1].hp == 0 &&
              !s->entities[1].alive);
    if (!ok) printf("  kill: lost=%d hp=%d alive=%d -> FAIL\n",
                    lost, s->entities[1].hp, s->entities[1].alive);
    lw_state_free(s);
    return ok;
}


static int test_kill_already_dead(void) {
    LwState *s = fresh_state();
    s->entities[1].alive = 0;
    s->entities[1].hp = 0;
    int lost = lw_apply_kill(s, 0, 1);
    int ok = (lost == 0);
    if (!ok) printf("  kill_dead: lost=%d -> FAIL\n", lost);
    lw_state_free(s);
    return ok;
}


static int test_add_state(void) {
    LwState *s = fresh_state();
    int rc = lw_apply_add_state(s, 1, LW_STATE_INVINCIBLE);
    int ok = (rc == 1 &&
              (s->entities[1].state_flags & LW_STATE_INVINCIBLE) != 0);
    if (!ok) printf("  add_state: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


static int test_add_state_idempotent(void) {
    /* Re-adding an already-set flag is a no-op (flag stays set). */
    LwState *s = fresh_state();
    s->entities[1].state_flags |= LW_STATE_UNHEALABLE;
    lw_apply_add_state(s, 1, LW_STATE_UNHEALABLE);
    int ok = ((s->entities[1].state_flags & LW_STATE_UNHEALABLE) != 0);
    if (!ok) printf("  add_state_idem: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


static int test_steal_absolute_shield(void) {
    /* Grants +previous_value to absolute shield. */
    LwState *s = fresh_state();
    int amt = lw_apply_steal_absolute_shield(s, 1, 80);
    int ok = (amt == 80 &&
              s->entities[1].buff_stats[LW_STAT_ABSOLUTE_SHIELD] == 80);
    if (!ok) printf("  steal_abs_shield: amt=%d -> FAIL\n", amt);
    lw_state_free(s);
    return ok;
}


static int test_steal_absolute_shield_zero(void) {
    LwState *s = fresh_state();
    int amt = lw_apply_steal_absolute_shield(s, 1, 0);
    int ok = (amt == 0 &&
              s->entities[1].buff_stats[LW_STAT_ABSOLUTE_SHIELD] == 0);
    if (!ok) printf("  steal_abs_zero: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


static int test_multiply_stats(void) {
    /* base str=50, ag=30; total_hp=1000; factor=3 -> str+=100, ag+=60,
     * total_hp += 1000*2 = 3000. hp scales to keep ratio. */
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[1];
    t->base_stats[LW_STAT_STRENGTH] = 50;
    t->base_stats[LW_STAT_AGILITY]  = 30;
    t->base_stats[LW_STAT_LIFE]     = 1000;
    t->total_hp = 1000;
    t->hp       = 1000;

    int factor = lw_apply_multiply_stats(s, 0, 1, 3.0);
    int ok = (factor == 3 &&
              t->buff_stats[LW_STAT_STRENGTH] == 100 &&
              t->buff_stats[LW_STAT_AGILITY] == 60 &&
              t->total_hp == 3000 &&
              t->hp == 3000);
    if (!ok) printf("  multiply: f=%d str=%d ag=%d total=%d hp=%d -> FAIL\n",
                    factor, t->buff_stats[LW_STAT_STRENGTH],
                    t->buff_stats[LW_STAT_AGILITY],
                    t->total_hp, t->hp);
    lw_state_free(s);
    return ok;
}


static int test_multiply_stats_noop(void) {
    /* factor=1 -> no-op. */
    LwState *s = fresh_state();
    s->entities[1].base_stats[LW_STAT_STRENGTH] = 50;
    int factor = lw_apply_multiply_stats(s, 0, 1, 1.0);
    int ok = (factor == 0 &&
              s->entities[1].buff_stats[LW_STAT_STRENGTH] == 0);
    if (!ok) printf("  multiply_noop: f=%d -> FAIL\n", factor);
    lw_state_free(s);
    return ok;
}


static int test_remove_shackles(void) {
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[1];

    /* Two shackles + a buff. Only the shackles should be removed. */
    LwEffect a, b, buff;
    lw_effect_init(&a);
    a.id = LW_EFFECT_SHACKLE_STRENGTH; a.value = 30;
    a.stats[LW_STAT_STRENGTH] = -30; a.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += -30;
    lw_effect_add(t, &a);

    lw_effect_init(&b);
    b.id = LW_EFFECT_SHACKLE_AGILITY; b.value = 20;
    b.stats[LW_STAT_AGILITY] = -20; b.turns = 3;
    t->buff_stats[LW_STAT_AGILITY] += -20;
    lw_effect_add(t, &b);

    lw_effect_init(&buff);
    buff.id = LW_EFFECT_BUFF_WISDOM; buff.value = 50;
    buff.stats[LW_STAT_WISDOM] = 50; buff.turns = 3;
    t->buff_stats[LW_STAT_WISDOM] += 50;
    lw_effect_add(t, &buff);

    int removed = lw_apply_remove_shackles(s, 1);
    int ok = (removed == 2 &&
              t->n_effects == 1 &&
              t->effects[0].id == LW_EFFECT_BUFF_WISDOM &&
              t->buff_stats[LW_STAT_STRENGTH] == 0 &&
              t->buff_stats[LW_STAT_AGILITY] == 0 &&
              t->buff_stats[LW_STAT_WISDOM] == 50);
    if (!ok) printf("  remove_shackles: removed=%d n=%d str=%d ag=%d wis=%d -> FAIL\n",
                    removed, t->n_effects,
                    t->buff_stats[LW_STAT_STRENGTH],
                    t->buff_stats[LW_STAT_AGILITY],
                    t->buff_stats[LW_STAT_WISDOM]);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_more_effects2:\n");
    int n = 0, ok = 0;
    n++; if (test_raw_buff_strength())              { printf("   1  raw_buff_strength OK\n"); ok++; }
    n++; if (test_raw_buff_ignores_caster_stat())   { printf("   2  raw_buff_ignores_caster OK\n"); ok++; }
    n++; if (test_vulnerability())                  { printf("   3  vulnerability OK\n"); ok++; }
    n++; if (test_absolute_vulnerability())         { printf("   4  absolute_vulnerability OK\n"); ok++; }
    n++; if (test_kill())                           { printf("   5  kill OK\n"); ok++; }
    n++; if (test_kill_already_dead())              { printf("   6  kill_already_dead OK\n"); ok++; }
    n++; if (test_add_state())                      { printf("   7  add_state OK\n"); ok++; }
    n++; if (test_add_state_idempotent())           { printf("   8  add_state_idempotent OK\n"); ok++; }
    n++; if (test_steal_absolute_shield())          { printf("   9  steal_abs_shield OK\n"); ok++; }
    n++; if (test_steal_absolute_shield_zero())     { printf("  10  steal_abs_shield_zero OK\n"); ok++; }
    n++; if (test_remove_shackles())                { printf("  11  remove_shackles OK\n"); ok++; }
    n++; if (test_multiply_stats())                 { printf("  12  multiply_stats OK\n"); ok++; }
    n++; if (test_multiply_stats_noop())            { printf("  13  multiply_stats_noop OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
