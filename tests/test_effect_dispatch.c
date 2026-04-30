/*
 * test_effect_dispatch.c -- Effect.createEffect dispatch.
 *
 * Verifies that the central dispatcher routes (effect_type, params)
 * to the correct apply_* function AND, when turns > 0 and value > 0,
 * registers a multi-turn entry whose stats[] correctly mirrors the
 * buff_stats[] delta (so removal cleanly unwinds).
 */

#include "lw_effect_dispatch.h"
#include "lw_effect.h"
#include "lw_effect_store.h"
#include "lw_state.h"
#include <stdio.h>
#include <string.h>


static LwState* fresh_state(void) {
    LwState *s = lw_state_alloc();
    s->n_entities = 2;
    LwEntity *c = &s->entities[0];
    LwEntity *t = &s->entities[1];
    memset(c, 0, sizeof(*c));
    memset(t, 0, sizeof(*t));
    c->id = 0; c->team_id = 0; c->alive = 1; c->cell_id = -1;
    c->hp = 1000; c->total_hp = 1000;
    t->id = 1; t->team_id = 1; t->alive = 1; t->cell_id = -1;
    t->hp = 1000; t->total_hp = 1000;
    return s;
}


static int test_damage_dispatch(void) {
    /* DAMAGE with strength=100, v1=80 v2=120 jet=0.5 -> 280 dealt.
     * Erosion = round(280 * 0.05) = 14, total_hp 1000->986. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_STRENGTH] = 100;

    LwEffectInput p = {0};
    p.type = LW_EFFECT_DAMAGE;
    p.caster_idx = 0; p.target_idx = 1;
    p.value1 = 80; p.value2 = 120; p.jet = 0.5;
    p.aoe = 1.0; p.target_count = 1; p.critical = 0;

    int v = lw_effect_create(s, &p);
    int ok = (v == 280 &&
              s->entities[1].hp == 720 &&
              s->entities[1].total_hp == 986);
    if (!ok) printf("  damage: v=%d hp=%d total=%d -> FAIL\n",
                    v, s->entities[1].hp, s->entities[1].total_hp);
    lw_state_free(s);
    return ok;
}


static int test_buff_strength_dispatch(void) {
    /* BUFF_STRENGTH with science=100, v1=30 v2=30 jet=0.5, turns=3
     * -> v=90, entry registered with stats[STRENGTH]=+90. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_SCIENCE] = 100;

    LwEffectInput p = {0};
    p.type = LW_EFFECT_BUFF_STRENGTH;
    p.caster_idx = 0; p.target_idx = 1;
    p.value1 = 30; p.value2 = 30; p.jet = 0.5;
    p.aoe = 1.0; p.target_count = 1; p.turns = 3;

    int v = lw_effect_create(s, &p);
    int ok = (v == 90 &&
              s->entities[1].buff_stats[LW_STAT_STRENGTH] == 90 &&
              s->entities[1].n_effects == 1 &&
              s->entities[1].effects[0].id == LW_EFFECT_BUFF_STRENGTH &&
              s->entities[1].effects[0].turns == 3 &&
              s->entities[1].effects[0].stats[LW_STAT_STRENGTH] == 90);
    if (!ok) printf("  buff_str: v=%d n=%d -> FAIL\n",
                    v, s->entities[1].n_effects);
    lw_state_free(s);
    return ok;
}


static int test_shackle_dispatch_records_negative(void) {
    /* SHACKLE_STRENGTH with magic=100 -> v=60, stats[STRENGTH]=-60. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_MAGIC] = 100;

    LwEffectInput p = {0};
    p.type = LW_EFFECT_SHACKLE_STRENGTH;
    p.caster_idx = 0; p.target_idx = 1;
    p.value1 = 20; p.value2 = 20; p.jet = 0.5;
    p.aoe = 1.0; p.target_count = 1; p.turns = 3;

    int v = lw_effect_create(s, &p);
    int ok = (v == 60 &&
              s->entities[1].buff_stats[LW_STAT_STRENGTH] == -60 &&
              s->entities[1].n_effects == 1 &&
              s->entities[1].effects[0].stats[LW_STAT_STRENGTH] == -60);
    if (!ok) printf("  shackle: v=%d buff=%d stat=%d -> FAIL\n",
                    v, s->entities[1].buff_stats[LW_STAT_STRENGTH],
                    s->entities[1].n_effects > 0 ? s->entities[1].effects[0].stats[LW_STAT_STRENGTH] : 0);
    lw_state_free(s);
    return ok;
}


static int test_poison_dispatch_creates_entry(void) {
    /* POISON: no immediate damage, just records per-turn value. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_MAGIC] = 100;

    LwEffectInput p = {0};
    p.type = LW_EFFECT_POISON;
    p.caster_idx = 0; p.target_idx = 1;
    p.value1 = 20; p.value2 = 10; p.jet = 0.5;
    p.aoe = 1.0; p.target_count = 1; p.turns = 4;

    int v = lw_effect_create(s, &p);
    /* Expected per-turn: (20+5)*(1+1)*1*1*1 = 50. */
    int ok = (v == 50 &&
              s->entities[1].hp == 1000 &&  /* no immediate damage */
              s->entities[1].n_effects == 1 &&
              s->entities[1].effects[0].id == LW_EFFECT_POISON &&
              s->entities[1].effects[0].value == 50 &&
              s->entities[1].effects[0].turns == 4);
    if (!ok) printf("  poison: v=%d hp=%d n=%d -> FAIL\n",
                    v, s->entities[1].hp, s->entities[1].n_effects);
    lw_state_free(s);
    return ok;
}


static int test_buff_unwind_via_decrement(void) {
    /* End-to-end: register a 1-turn buff via dispatcher, decrement,
     * confirm buff_stats unwound. */
    LwState *s = fresh_state();

    LwEffectInput p = {0};
    p.type = LW_EFFECT_BUFF_AGILITY;
    p.caster_idx = 0; p.target_idx = 1;
    p.value1 = 30; p.value2 = 30; p.jet = 0.5;
    p.aoe = 1.0; p.target_count = 1; p.turns = 1;

    /* No science -> v = 45. */
    int v = lw_effect_create(s, &p);
    int after_create = s->entities[1].buff_stats[LW_STAT_AGILITY];

    int expired = lw_effect_decrement_turns(&s->entities[1]);
    int after_decrement = s->entities[1].buff_stats[LW_STAT_AGILITY];

    int ok = (v == 45 &&
              after_create == 45 &&
              expired == 1 &&
              after_decrement == 0 &&
              s->entities[1].n_effects == 0);
    if (!ok) printf("  unwind: v=%d after=%d/%d expired=%d -> FAIL\n",
                    v, after_create, after_decrement, expired);
    lw_state_free(s);
    return ok;
}


static int test_kill_dispatch(void) {
    LwState *s = fresh_state();
    LwEffectInput p = {0};
    p.type = LW_EFFECT_KILL;
    p.caster_idx = 0; p.target_idx = 1;
    p.aoe = 1.0;

    int v = lw_effect_create(s, &p);
    int ok = (v == 1000 && !s->entities[1].alive);
    if (!ok) printf("  kill: v=%d alive=%d -> FAIL\n",
                    v, s->entities[1].alive);
    lw_state_free(s);
    return ok;
}


static int test_passive_marker_returns_zero(void) {
    /* DAMAGE_TO_ABSOLUTE_SHIELD (id 34) is a passive marker — Python
     * Effect class is None. Dispatcher should return 0 with no
     * state changes. */
    LwState *s = fresh_state();

    LwEffectInput p = {0};
    p.type = 34;  /* DAMAGE_TO_ABSOLUTE_SHIELD */
    p.caster_idx = 0; p.target_idx = 1;
    p.aoe = 1.0; p.value1 = 100;

    int v = lw_effect_create(s, &p);
    int ok = (v == 0 && s->entities[1].n_effects == 0);
    if (!ok) printf("  passive: v=%d -> FAIL\n", v);
    lw_state_free(s);
    return ok;
}


static int test_steal_life_dispatch(void) {
    LwState *s = fresh_state();
    s->entities[1].hp = 800;
    LwEffectInput p = {0};
    p.type = LW_EFFECT_STEAL_LIFE;
    p.target_idx = 1;
    p.previous_value = 100;

    int v = lw_effect_create(s, &p);
    int ok = (v == 100 && s->entities[1].hp == 900);
    if (!ok) printf("  steal_life: v=%d hp=%d -> FAIL\n",
                    v, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_add_state_dispatch(void) {
    LwState *s = fresh_state();
    LwEffectInput p = {0};
    p.type = LW_EFFECT_ADD_STATE;
    p.target_idx = 1;
    p.value1 = LW_STATE_INVINCIBLE;

    int v = lw_effect_create(s, &p);
    int ok = (v == 1 &&
              (s->entities[1].state_flags & LW_STATE_INVINCIBLE) != 0);
    if (!ok) printf("  add_state: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_effect_dispatch:\n");
    int n = 0, ok = 0;
    n++; if (test_damage_dispatch())               { printf("   1  damage_dispatch OK\n"); ok++; }
    n++; if (test_buff_strength_dispatch())        { printf("   2  buff_strength_dispatch OK\n"); ok++; }
    n++; if (test_shackle_dispatch_records_negative()){ printf("   3  shackle_neg_record OK\n"); ok++; }
    n++; if (test_poison_dispatch_creates_entry()) { printf("   4  poison_creates_entry OK\n"); ok++; }
    n++; if (test_buff_unwind_via_decrement())     { printf("   5  buff_unwind OK\n"); ok++; }
    n++; if (test_kill_dispatch())                 { printf("   6  kill_dispatch OK\n"); ok++; }
    n++; if (test_passive_marker_returns_zero())   { printf("   7  passive_marker OK\n"); ok++; }
    n++; if (test_steal_life_dispatch())           { printf("   8  steal_life_dispatch OK\n"); ok++; }
    n++; if (test_add_state_dispatch())            { printf("   9  add_state_dispatch OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
