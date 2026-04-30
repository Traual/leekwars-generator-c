/*
 * test_turn.c -- start-of-turn tick + end-of-turn cleanup integration.
 *
 * Verifies the per-tick lifecycle:
 *   1. Active POISON / AFTEREFFECT effects deal damage at start-of-turn.
 *   2. Multi-turn HEAL effects heal at start-of-turn (capped).
 *   3. End-of-turn decrements counters; expired effects vanish AND
 *      their buff_stats[] deltas are unwound.
 */

#include "lw_turn.h"
#include "lw_effects.h"
#include "lw_effect_store.h"
#include "lw_state.h"
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


static int test_poison_ticks_at_start(void) {
    LwState *s = fresh_state();
    LwEffect p;
    lw_effect_init(&p);
    p.id = LW_EFFECT_POISON; p.value = 80; p.turns = 3;
    lw_effect_add(&s->entities[0], &p);

    int dmg = lw_turn_start(s, 0);
    int ok = (dmg == 80 && s->entities[0].hp == 920);
    if (!ok) printf("  poison_tick: dmg=%d hp=%d -> FAIL\n",
                    dmg, s->entities[0].hp);
    lw_state_free(s);
    return ok;
}


static int test_aftereffect_ticks_at_start(void) {
    LwState *s = fresh_state();
    LwEffect ae;
    lw_effect_init(&ae);
    ae.id = LW_EFFECT_AFTEREFFECT; ae.value = 60; ae.turns = 4;
    lw_effect_add(&s->entities[0], &ae);

    int dmg = lw_turn_start(s, 0);
    int ok = (dmg == 60 && s->entities[0].hp == 940);
    if (!ok) printf("  aftereffect_tick: dmg=%d hp=%d -> FAIL\n",
                    dmg, s->entities[0].hp);
    lw_state_free(s);
    return ok;
}


static int test_heal_ticks_subtract(void) {
    LwState *s = fresh_state();
    s->entities[0].hp = 700;
    LwEffect h;
    lw_effect_init(&h);
    h.id = LW_EFFECT_HEAL; h.value = 100; h.turns = 3;
    lw_effect_add(&s->entities[0], &h);

    int dmg = lw_turn_start(s, 0);
    int ok = (dmg == -100 && s->entities[0].hp == 800);
    if (!ok) printf("  heal_tick: dmg=%d hp=%d -> FAIL\n",
                    dmg, s->entities[0].hp);
    lw_state_free(s);
    return ok;
}


static int test_mixed_effects_ticks(void) {
    /* Three effects ticking in one turn: poison 50, heal 30,
     * aftereffect 20. Net damage = 50 + 20 - 30 = 40. */
    LwState *s = fresh_state();
    s->entities[0].hp = 700;
    LwEffect p, h, a;
    lw_effect_init(&p); p.id = LW_EFFECT_POISON; p.value = 50; p.turns = 3;
    lw_effect_init(&h); h.id = LW_EFFECT_HEAL; h.value = 30; h.turns = 3;
    lw_effect_init(&a); a.id = LW_EFFECT_AFTEREFFECT; a.value = 20; a.turns = 3;
    lw_effect_add(&s->entities[0], &p);
    lw_effect_add(&s->entities[0], &h);
    lw_effect_add(&s->entities[0], &a);

    int dmg = lw_turn_start(s, 0);
    /* hp = 700 - 50 + 30 - 20 = 660; net dmg report = 50 - 30 + 20 = 40. */
    int ok = (dmg == 40 && s->entities[0].hp == 660);
    if (!ok) printf("  mixed: dmg=%d hp=%d -> FAIL\n",
                    dmg, s->entities[0].hp);
    lw_state_free(s);
    return ok;
}


static int test_end_turn_decrements_and_unwinds(void) {
    LwState *s = fresh_state();
    LwEffect buff;
    lw_effect_init(&buff);
    buff.id = LW_EFFECT_BUFF_STRENGTH; buff.value = 50;
    buff.stats[LW_STAT_STRENGTH] = 50; buff.turns = 1;
    s->entities[0].buff_stats[LW_STAT_STRENGTH] += 50;
    lw_effect_add(&s->entities[0], &buff);

    int expired = lw_turn_end(s, 0);
    int ok = (expired == 1 &&
              s->entities[0].n_effects == 0 &&
              s->entities[0].buff_stats[LW_STAT_STRENGTH] == 0);
    if (!ok) printf("  end_turn: expired=%d n=%d buff=%d -> FAIL\n",
                    expired, s->entities[0].n_effects,
                    s->entities[0].buff_stats[LW_STAT_STRENGTH]);
    lw_state_free(s);
    return ok;
}


static int test_end_turn_keeps_active(void) {
    LwState *s = fresh_state();
    LwEffect buff;
    lw_effect_init(&buff);
    buff.id = LW_EFFECT_BUFF_AGILITY; buff.value = 30;
    buff.stats[LW_STAT_AGILITY] = 30; buff.turns = 3;
    s->entities[0].buff_stats[LW_STAT_AGILITY] += 30;
    lw_effect_add(&s->entities[0], &buff);

    int expired = lw_turn_end(s, 0);
    int ok = (expired == 0 &&
              s->entities[0].n_effects == 1 &&
              s->entities[0].effects[0].turns == 2 &&
              s->entities[0].buff_stats[LW_STAT_AGILITY] == 30);
    if (!ok) printf("  end_turn_keep: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


static int test_end_turn_all_skips_dead(void) {
    LwState *s = lw_state_alloc();
    s->n_entities = 2;
    for (int i = 0; i < 2; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i;
        s->entities[i].alive = (i == 0) ? 1 : 0;  /* second dead */
    }

    LwEffect e1, e2;
    lw_effect_init(&e1); e1.id = LW_EFFECT_BUFF_MP; e1.turns = 1;
    e1.stats[LW_STAT_MP] = 5; s->entities[0].buff_stats[LW_STAT_MP] += 5;
    lw_effect_add(&s->entities[0], &e1);

    lw_effect_init(&e2); e2.id = LW_EFFECT_BUFF_MP; e2.turns = 1;
    e2.stats[LW_STAT_MP] = 8; s->entities[1].buff_stats[LW_STAT_MP] += 8;
    lw_effect_add(&s->entities[1], &e2);

    int expired = lw_turn_end_all(s);
    /* Only the alive entity's effects expire. The dead entity's
     * effects stay (no tick on dead). */
    int ok = (expired == 1 &&
              s->entities[0].n_effects == 0 &&
              s->entities[1].n_effects == 1);
    if (!ok) printf("  end_all: expired=%d alive_n=%d dead_n=%d -> FAIL\n",
                    expired, s->entities[0].n_effects,
                    s->entities[1].n_effects);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_turn:\n");
    int n = 0, ok = 0;
    n++; if (test_poison_ticks_at_start())          { printf("   1  poison_tick OK\n"); ok++; }
    n++; if (test_aftereffect_ticks_at_start())     { printf("   2  aftereffect_tick OK\n"); ok++; }
    n++; if (test_heal_ticks_subtract())            { printf("   3  heal_tick OK\n"); ok++; }
    n++; if (test_mixed_effects_ticks())            { printf("   4  mixed_effects OK\n"); ok++; }
    n++; if (test_end_turn_decrements_and_unwinds()){ printf("   5  end_turn_unwinds OK\n"); ok++; }
    n++; if (test_end_turn_keeps_active())          { printf("   6  end_turn_keeps_active OK\n"); ok++; }
    n++; if (test_end_turn_all_skips_dead())        { printf("   7  end_turn_all_skips_dead OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
