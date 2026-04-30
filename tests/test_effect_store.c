/*
 * test_effect_store.c -- effect-list manipulation primitives.
 *
 * Covers add / remove / decrement / reduce / cleanup paths. The
 * underlying invariants we want to keep:
 *
 *   1. Adding an effect doesn't touch buff_stats[] directly -- the
 *      caller did that already (the lw_apply_*() helpers write the
 *      buff first, then ask the store to track it).
 *   2. Removing an effect undoes its stats[] against buff_stats[],
 *      restoring the entity to its pre-effect state.
 *   3. Decrement only ticks down counters > 0 and removes effects
 *      that hit 0; permanent effects (turns == 0 from the start) are
 *      left alone.
 *   4. Reduce/reduce_total scale value AND stats[] together, with
 *     buff_stats[] kept consistent throughout.
 */

#include "lw_effect_store.h"
#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


static LwState* fresh_state(void) {
    LwState *s = lw_state_alloc();
    s->n_entities = 1;
    LwEntity *e = &s->entities[0];
    memset(e, 0, sizeof(*e));
    e->id = 0; e->fid = 0; e->team_id = 0; e->alive = 1; e->cell_id = -1;
    e->hp = 1000; e->total_hp = 1000;
    return s;
}


static int test_init_defaults(void) {
    LwEffect e;
    lw_effect_init(&e);
    int ok = (e.id == 0 && e.turns == 0 &&
              e.aoe == 1.0 && e.critical_power == 1.0 &&
              e.target_id == -1 && e.caster_id == -1 && e.attack_id == -1);
    if (!ok) printf("  init_defaults: FAIL\n");
    return ok;
}


static int test_add_then_remove(void) {
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];

    /* A simple buff: +50 strength. The "caller" applied it to
     * buff_stats first, then registered the effect. */
    t->buff_stats[LW_STAT_STRENGTH] += 50;

    LwEffect eff;
    lw_effect_init(&eff);
    eff.id = LW_EFFECT_BUFF_STRENGTH;
    eff.turns = 3;
    eff.value = 50;
    eff.stats[LW_STAT_STRENGTH] = 50;

    int slot = lw_effect_add(t, &eff);
    int ok1 = (slot == 0 && t->n_effects == 1 &&
               t->buff_stats[LW_STAT_STRENGTH] == 50);

    /* Removing the effect should unwind the +50. */
    lw_effect_remove(t, 0);
    int ok2 = (t->n_effects == 0 &&
               t->buff_stats[LW_STAT_STRENGTH] == 0);

    int ok = ok1 && ok2;
    if (!ok) printf("  add_then_remove: ok1=%d ok2=%d -> FAIL\n", ok1, ok2);
    lw_state_free(s);
    return ok;
}


static int test_add_full(void) {
    /* Filling all LW_MAX_EFFECTS slots returns -1 for the next add. */
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];
    LwEffect eff;
    lw_effect_init(&eff);
    eff.id = LW_EFFECT_VITALITY;
    eff.turns = 2;

    for (int i = 0; i < LW_MAX_EFFECTS; i++) {
        int slot = lw_effect_add(t, &eff);
        if (slot != i) {
            printf("  add_full: slot=%d expected=%d -> FAIL\n", slot, i);
            lw_state_free(s);
            return 0;
        }
    }
    int slot = lw_effect_add(t, &eff);
    int ok = (slot == -1 && t->n_effects == LW_MAX_EFFECTS);
    if (!ok) printf("  add_full: slot=%d (after full) -> FAIL\n", slot);
    lw_state_free(s);
    return ok;
}


static int test_remove_by_id(void) {
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];

    /* Two strength buffs from different attacks, plus a heal. */
    LwEffect a, b, c;
    lw_effect_init(&a);
    a.id = LW_EFFECT_BUFF_STRENGTH; a.caster_id = 1; a.attack_id = 100;
    a.value = 30; a.stats[LW_STAT_STRENGTH] = 30; a.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += 30;
    lw_effect_add(t, &a);

    lw_effect_init(&b);
    b.id = LW_EFFECT_BUFF_STRENGTH; b.caster_id = 1; b.attack_id = 200;
    b.value = 40; b.stats[LW_STAT_STRENGTH] = 40; b.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += 40;
    lw_effect_add(t, &b);

    lw_effect_init(&c);
    c.id = LW_EFFECT_HEAL; c.caster_id = 1; c.attack_id = 300;
    c.turns = 2;
    lw_effect_add(t, &c);

    int ok1 = (t->n_effects == 3 &&
               t->buff_stats[LW_STAT_STRENGTH] == 70);

    /* Removing the strength buff cast via attack 100 should drop it
     * AND unwind its +30 contribution. The +40 from attack 200 stays. */
    int removed = lw_effect_remove_by_id(t, LW_EFFECT_BUFF_STRENGTH, 1, 100);
    int ok2 = (removed == 1 && t->n_effects == 2 &&
               t->buff_stats[LW_STAT_STRENGTH] == 40);

    /* Removing a non-existent (caster, attack) returns 0 and is a noop. */
    int removed_none = lw_effect_remove_by_id(t, LW_EFFECT_BUFF_STRENGTH, 1, 999);
    int ok3 = (removed_none == 0 && t->n_effects == 2);

    int ok = ok1 && ok2 && ok3;
    if (!ok) printf("  remove_by_id: ok1=%d ok2=%d ok3=%d -> FAIL\n",
                    ok1, ok2, ok3);
    lw_state_free(s);
    return ok;
}


static int test_clear_poisons(void) {
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];

    LwEffect p1, p2, buff;
    lw_effect_init(&p1);
    p1.id = LW_EFFECT_POISON; p1.value = 50; p1.turns = 4;
    lw_effect_add(t, &p1);

    lw_effect_init(&p2);
    p2.id = LW_EFFECT_POISON; p2.value = 30; p2.turns = 3;
    lw_effect_add(t, &p2);

    lw_effect_init(&buff);
    buff.id = LW_EFFECT_BUFF_STRENGTH;
    buff.value = 20; buff.stats[LW_STAT_STRENGTH] = 20; buff.turns = 5;
    t->buff_stats[LW_STAT_STRENGTH] += 20;
    lw_effect_add(t, &buff);

    int n = lw_effect_clear_poisons(t);
    int ok = (n == 2 && t->n_effects == 1 &&
              t->effects[0].id == LW_EFFECT_BUFF_STRENGTH &&
              t->buff_stats[LW_STAT_STRENGTH] == 20);
    if (!ok) printf("  clear_poisons: removed=%d n_effects=%d -> FAIL\n",
                    n, t->n_effects);
    lw_state_free(s);
    return ok;
}


static int test_decrement_turns(void) {
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];

    LwEffect a, b, perm;
    lw_effect_init(&a);
    a.id = LW_EFFECT_POISON; a.value = 50; a.turns = 1;
    lw_effect_add(t, &a);

    lw_effect_init(&b);
    b.id = LW_EFFECT_BUFF_STRENGTH; b.value = 10;
    b.stats[LW_STAT_STRENGTH] = 10; b.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += 10;
    lw_effect_add(t, &b);

    lw_effect_init(&perm);
    perm.id = LW_EFFECT_DAMAGE_RETURN; perm.turns = 0;  /* permanent */
    lw_effect_add(t, &perm);

    int removed = lw_effect_decrement_turns(t);
    /* a expired (turns 1->0, removed). b ticks 3->2 (kept). perm
     * stays at 0. */
    int ok = (removed == 1 && t->n_effects == 2 &&
              t->effects[0].id == LW_EFFECT_BUFF_STRENGTH &&
              t->effects[0].turns == 2 &&
              t->effects[1].id == LW_EFFECT_DAMAGE_RETURN &&
              t->effects[1].turns == 0 &&
              t->buff_stats[LW_STAT_STRENGTH] == 10);
    if (!ok) printf("  decrement_turns: removed=%d n_effects=%d -> FAIL\n",
                    removed, t->n_effects);
    lw_state_free(s);
    return ok;
}


static int test_reduce(void) {
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];

    /* Two buffs: one regular (+40 strength), one irreductible (+30 agility). */
    LwEffect a, b;
    lw_effect_init(&a);
    a.id = LW_EFFECT_BUFF_STRENGTH; a.value = 40;
    a.stats[LW_STAT_STRENGTH] = 40; a.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += 40;
    lw_effect_add(t, &a);

    lw_effect_init(&b);
    b.id = LW_EFFECT_BUFF_AGILITY; b.value = 30;
    b.stats[LW_STAT_AGILITY] = 30; b.turns = 3;
    b.modifiers = LW_MODIFIER_IRREDUCTIBLE;
    t->buff_stats[LW_STAT_AGILITY] += 30;
    lw_effect_add(t, &b);

    /* Reduce by 50%: factor = 0.5. Strength buff -> 40*0.5 = 20.
     * Agility buff is irreductible -> stays at 30. */
    lw_effect_reduce(t, 0.5);
    int ok = (t->effects[0].value == 20 &&
              t->effects[0].stats[LW_STAT_STRENGTH] == 20 &&
              t->buff_stats[LW_STAT_STRENGTH] == 20 &&
              t->effects[1].value == 30 &&
              t->effects[1].stats[LW_STAT_AGILITY] == 30 &&
              t->buff_stats[LW_STAT_AGILITY] == 30);
    if (!ok) {
        printf("  reduce: str=%d str_buff=%d ag=%d ag_buff=%d -> FAIL\n",
               t->effects[0].value, t->buff_stats[LW_STAT_STRENGTH],
               t->effects[1].value, t->buff_stats[LW_STAT_AGILITY]);
    }
    lw_state_free(s);
    return ok;
}


static int test_reduce_total(void) {
    /* Same as reduce, but irreductible flag is ignored. */
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];

    LwEffect a, b;
    lw_effect_init(&a);
    a.id = LW_EFFECT_BUFF_STRENGTH; a.value = 40;
    a.stats[LW_STAT_STRENGTH] = 40; a.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += 40;
    lw_effect_add(t, &a);

    lw_effect_init(&b);
    b.id = LW_EFFECT_BUFF_AGILITY; b.value = 30;
    b.stats[LW_STAT_AGILITY] = 30; b.turns = 3;
    b.modifiers = LW_MODIFIER_IRREDUCTIBLE;
    t->buff_stats[LW_STAT_AGILITY] += 30;
    lw_effect_add(t, &b);

    lw_effect_reduce_total(t, 0.5);
    int ok = (t->effects[0].value == 20 &&
              t->buff_stats[LW_STAT_STRENGTH] == 20 &&
              t->effects[1].value == 15 &&
              t->buff_stats[LW_STAT_AGILITY] == 15);
    if (!ok) {
        printf("  reduce_total: str=%d str_buff=%d ag=%d ag_buff=%d -> FAIL\n",
               t->effects[0].value, t->buff_stats[LW_STAT_STRENGTH],
               t->effects[1].value, t->buff_stats[LW_STAT_AGILITY]);
    }
    lw_state_free(s);
    return ok;
}


static int test_reduce_negative_buff(void) {
    /* Shackles are stored as negative stat deltas. Reducing them should
     * also pull buff_stats up toward 0 (less debuff after the reduce). */
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[0];

    LwEffect shackle;
    lw_effect_init(&shackle);
    shackle.id = LW_EFFECT_SHACKLE_STRENGTH; shackle.value = 60;
    shackle.stats[LW_STAT_STRENGTH] = -60; shackle.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += -60;
    lw_effect_add(t, &shackle);

    /* Reduce by 50% -> abs(60)*0.5 = 30, signed back to -30. */
    lw_effect_reduce(t, 0.5);
    int ok = (t->effects[0].value == 30 &&
              t->effects[0].stats[LW_STAT_STRENGTH] == -30 &&
              t->buff_stats[LW_STAT_STRENGTH] == -30);
    if (!ok) printf("  reduce_negative: stats=%d buff=%d -> FAIL\n",
                    t->effects[0].stats[LW_STAT_STRENGTH],
                    t->buff_stats[LW_STAT_STRENGTH]);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_effect_store:\n");
    int n = 0, ok = 0;
    n++; if (test_init_defaults())          { printf("   1  init_defaults OK\n"); ok++; }
    n++; if (test_add_then_remove())        { printf("   2  add_then_remove OK\n"); ok++; }
    n++; if (test_add_full())               { printf("   3  add_full OK\n"); ok++; }
    n++; if (test_remove_by_id())           { printf("   4  remove_by_id OK\n"); ok++; }
    n++; if (test_clear_poisons())          { printf("   5  clear_poisons OK\n"); ok++; }
    n++; if (test_decrement_turns())        { printf("   6  decrement_turns OK\n"); ok++; }
    n++; if (test_reduce())                 { printf("   7  reduce OK\n"); ok++; }
    n++; if (test_reduce_total())           { printf("   8  reduce_total OK\n"); ok++; }
    n++; if (test_reduce_negative_buff())   { printf("   9  reduce_negative_buff OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
