/*
 * test_more_effects_parity.c -- shackles, vitality, raw_heal,
 * nova_damage, life_damage, debuff/total_debuff, antidote, plus the
 * tick paths (aftereffect tick, heal tick).
 *
 * Each formula was double-checked against the leekwars/effect/ Python
 * sources.
 * Expected numeric outcomes were derived from the Python formulas
 * by hand.
 */

#include "lw_damage.h"
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


static int test_shackle_strength(void) {
    /* (v1=20, v2=20, jet=0.5, magic=100, aoe=1, crit=1)
     * v = (20 + 0.5*20)(1 + 1)*1*1 = 30*2 = 60.
     * Target buff_stats[STRENGTH] becomes -60.
     */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_MAGIC] = 100;
    int amt = lw_apply_shackle(s, 0, 1, LW_STAT_STRENGTH,
                                20, 20, 0.5, 1.0, 1.0);
    int ok = (amt == 60 &&
              s->entities[1].buff_stats[LW_STAT_STRENGTH] == -60);
    if (!ok) printf("  shackle_str: amt=%d buff=%d expected 60/-60 -> FAIL\n",
                    amt, s->entities[1].buff_stats[LW_STAT_STRENGTH]);
    lw_state_free(s);
    return ok;
}


static int test_shackle_negative_magic_clamped(void) {
    /* magic = -200 -> clamped to 0 -> v = (20+10)*(1+0)*1 = 30. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_MAGIC] = -200;
    int amt = lw_apply_shackle(s, 0, 1, LW_STAT_MP,
                                20, 20, 0.5, 1.0, 1.0);
    int ok = (amt == 30 &&
              s->entities[1].buff_stats[LW_STAT_MP] == -30);
    if (!ok) printf("  shackle_neg_magic: amt=%d -> FAIL\n", amt);
    lw_state_free(s);
    return ok;
}


static int test_vitality(void) {
    /* (v1=50, v2=50, jet=0.5, wisdom=100, aoe=1, crit=1)
     * v = (50 + 25)(1 + 1) * 1 = 75 * 2 = 150.
     * Target hp 1000 -> 1150, total_hp 1000 -> 1150.
     */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_WISDOM] = 100;
    int amt = lw_apply_vitality(s, 0, 1, 50, 50, 0.5, 1.0, 1.0);
    int ok = (amt == 150 &&
              s->entities[1].hp == 1150 &&
              s->entities[1].total_hp == 1150);
    if (!ok) printf("  vitality: amt=%d hp=%d total=%d -> FAIL\n",
                    amt, s->entities[1].hp, s->entities[1].total_hp);
    lw_state_free(s);
    return ok;
}


static int test_nova_vitality(void) {
    /* science=100, v=150 like above but only total_hp grows. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_SCIENCE] = 100;
    int amt = lw_apply_nova_vitality(s, 0, 1, 50, 50, 0.5, 1.0, 1.0);
    int ok = (amt == 150 &&
              s->entities[1].hp == 1000 &&
              s->entities[1].total_hp == 1150);
    if (!ok) printf("  nova_vitality: amt=%d hp=%d total=%d -> FAIL\n",
                    amt, s->entities[1].hp, s->entities[1].total_hp);
    lw_state_free(s);
    return ok;
}


static int test_raw_heal(void) {
    /* No wisdom scaling. (v1=40, v2=40, jet=0.5, aoe=1, crit=1, tc=2)
     * v = (40 + 20) * 1 * 1 * 2 = 120.
     * Missing HP 200 -> heals 120.
     */
    LwState *s = fresh_state();
    s->entities[1].hp = 800;
    int healed = lw_apply_raw_heal(s, 0, 1, 40, 40, 0.5, 1.0, 1.0, 2);
    int ok = (healed == 120 && s->entities[1].hp == 920);
    if (!ok) printf("  raw_heal: healed=%d hp=%d -> FAIL\n",
                    healed, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_raw_heal_capped(void) {
    LwState *s = fresh_state();
    s->entities[1].hp = 990;  /* missing 10 */
    int healed = lw_apply_raw_heal(s, 0, 1, 40, 40, 0.5, 1.0, 1.0, 2);
    int ok = (healed == 10 && s->entities[1].hp == 1000);
    if (!ok) printf("  raw_heal_capped: healed=%d -> FAIL\n", healed);
    lw_state_free(s);
    return ok;
}


static int test_nova_damage(void) {
    /* (v1=80, v2=40, jet=0.5, science=100, power=0, aoe=1, crit=1)
     * d = (80+20)*(1+1)*1*1*1 = 200. Target missing HP 300 -> applied=200.
     * total_hp becomes 1000-200=800; current HP unchanged.
     */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_SCIENCE] = 100;
    s->entities[1].hp = 700;
    int amt = lw_apply_nova_damage(s, 0, 1, 80, 40, 0.5, 1.0, 1.0);
    int ok = (amt == 200 &&
              s->entities[1].total_hp == 800 &&
              s->entities[1].hp == 700);
    if (!ok) printf("  nova_dmg: amt=%d total=%d hp=%d -> FAIL\n",
                    amt, s->entities[1].total_hp, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_nova_damage_capped_at_gap(void) {
    /* Missing HP = 20 -> nova damage capped at 20 even if formula gives 200. */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_SCIENCE] = 100;
    s->entities[1].hp = 980;  /* missing 20 */
    int amt = lw_apply_nova_damage(s, 0, 1, 80, 40, 0.5, 1.0, 1.0);
    int ok = (amt == 20 &&
              s->entities[1].total_hp == 980 &&
              s->entities[1].hp == 980);
    if (!ok) printf("  nova_dmg_cap: amt=%d total=%d hp=%d -> FAIL\n",
                    amt, s->entities[1].total_hp, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_life_damage_basic(void) {
    /* (v1=10, v2=10, jet=0.5, caster_life=1000, power=0, aoe=1, crit=1)
     * d = (10+5)/100 * 1000 * 1 * 1 * 1 = 150.
     * No shields -> dealt = 150. Target hp 1000 -> 850.
     */
    LwState *s = fresh_state();
    int dealt = lw_apply_life_damage(s, 0, 1, 10, 10, 0.5, 1.0, 1.0);
    int ok = (dealt == 150 && s->entities[1].hp == 850);
    if (!ok) printf("  life_dmg: dealt=%d hp=%d -> FAIL\n",
                    dealt, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_life_damage_with_reflect(void) {
    /* d=150 (as above); damage_return=100% -> reflect 150.
     * Target hp 1000 -> 850. Caster hp 1000 -> 850. */
    LwState *s = fresh_state();
    s->entities[1].base_stats[LW_STAT_DAMAGE_RETURN] = 100;
    int dealt = lw_apply_life_damage(s, 0, 1, 10, 10, 0.5, 1.0, 1.0);
    int ok = (dealt == 150 &&
              s->entities[1].hp == 850 &&
              s->entities[0].hp == 850);
    if (!ok) printf("  life_dmg_reflect: dealt=%d t=%d c=%d -> FAIL\n",
                    dealt, s->entities[1].hp, s->entities[0].hp);
    lw_state_free(s);
    return ok;
}


static int test_life_damage_self(void) {
    /* Self-targeted: no reflect even with damage_return.
     * caster_life starts 1000; d = 150; caster takes 150; final hp 850.
     * (Reflect path skipped because target == caster.) */
    LwState *s = fresh_state();
    s->entities[0].base_stats[LW_STAT_DAMAGE_RETURN] = 100;
    int dealt = lw_apply_life_damage(s, 0, 0, 10, 10, 0.5, 1.0, 1.0);
    int ok = (dealt == 150 && s->entities[0].hp == 850);
    if (!ok) printf("  life_dmg_self: dealt=%d hp=%d -> FAIL\n",
                    dealt, s->entities[0].hp);
    lw_state_free(s);
    return ok;
}


static int test_steal_life(void) {
    /* Heals by previous_value, capped at missing HP. */
    LwState *s = fresh_state();
    s->entities[1].hp = 800;
    int healed = lw_apply_steal_life(s, 1, 150);
    int ok = (healed == 150 && s->entities[1].hp == 950);
    if (!ok) printf("  steal_life: healed=%d hp=%d -> FAIL\n",
                    healed, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_steal_life_unhealable(void) {
    LwState *s = fresh_state();
    s->entities[1].hp = 800;
    s->entities[1].state_flags |= LW_STATE_UNHEALABLE;
    int healed = lw_apply_steal_life(s, 1, 150);
    int ok = (healed == 0 && s->entities[1].hp == 800);
    if (!ok) printf("  steal_life_unhealable: healed=%d -> FAIL\n", healed);
    lw_state_free(s);
    return ok;
}


static int test_antidote(void) {
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[1];
    LwEffect p1, p2;
    lw_effect_init(&p1);
    p1.id = LW_EFFECT_POISON; p1.value = 50; p1.turns = 4;
    lw_effect_add(t, &p1);
    lw_effect_init(&p2);
    p2.id = LW_EFFECT_POISON; p2.value = 30; p2.turns = 3;
    lw_effect_add(t, &p2);
    int cleared = lw_apply_antidote(s, 1);
    int ok = (cleared == 2 && t->n_effects == 0);
    if (!ok) printf("  antidote: cleared=%d n=%d -> FAIL\n",
                    cleared, t->n_effects);
    lw_state_free(s);
    return ok;
}


static int test_debuff(void) {
    /* Reduce buff effects by 50%. (v1=50, v2=0, aoe=1, crit=1, tc=1)
     * raw = (50+0)*1*1*1 = 50 -> percent = 0.5.
     */
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[1];

    LwEffect b;
    lw_effect_init(&b);
    b.id = LW_EFFECT_BUFF_STRENGTH; b.value = 60;
    b.stats[LW_STAT_STRENGTH] = 60; b.turns = 3;
    t->buff_stats[LW_STAT_STRENGTH] += 60;
    lw_effect_add(t, &b);

    int amt = lw_apply_debuff(s, 0, 1, 50, 0, 0.5, 1.0, 1.0, 1);
    int ok = (amt == 50 &&
              t->effects[0].value == 30 &&
              t->buff_stats[LW_STAT_STRENGTH] == 30);
    if (!ok) printf("  debuff: amt=%d val=%d buff=%d -> FAIL\n",
                    amt, t->effects[0].value, t->buff_stats[LW_STAT_STRENGTH]);
    lw_state_free(s);
    return ok;
}


static int test_total_debuff_strips_irreductible(void) {
    /* Same as debuff but irreductible buff also gets reduced. */
    LwState *s = fresh_state();
    LwEntity *t = &s->entities[1];

    LwEffect b;
    lw_effect_init(&b);
    b.id = LW_EFFECT_BUFF_AGILITY; b.value = 40;
    b.stats[LW_STAT_AGILITY] = 40; b.turns = 3;
    b.modifiers = LW_MODIFIER_IRREDUCTIBLE;
    t->buff_stats[LW_STAT_AGILITY] += 40;
    lw_effect_add(t, &b);

    int amt = lw_apply_total_debuff(s, 0, 1, 50, 0, 0.5, 1.0, 1.0, 1);
    int ok = (amt == 50 &&
              t->effects[0].value == 20 &&
              t->buff_stats[LW_STAT_AGILITY] == 20);
    if (!ok) printf("  total_debuff: amt=%d val=%d buff=%d -> FAIL\n",
                    amt, t->effects[0].value, t->buff_stats[LW_STAT_AGILITY]);
    lw_state_free(s);
    return ok;
}


static int test_aftereffect_tick(void) {
    /* per_turn_damage=80, hp 1000 -> 920. */
    LwState *s = fresh_state();
    int dealt = lw_tick_aftereffect(s, 1, 80);
    int ok = (dealt == 80 && s->entities[1].hp == 920);
    if (!ok) printf("  aftereffect_tick: dealt=%d hp=%d -> FAIL\n",
                    dealt, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_heal_tick(void) {
    LwState *s = fresh_state();
    s->entities[1].hp = 700;
    int healed = lw_tick_heal(s, 1, 200);
    int ok = (healed == 200 && s->entities[1].hp == 900);
    if (!ok) printf("  heal_tick: healed=%d hp=%d -> FAIL\n",
                    healed, s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_heal_tick_unhealable(void) {
    LwState *s = fresh_state();
    s->entities[1].hp = 700;
    s->entities[1].state_flags |= LW_STATE_UNHEALABLE;
    int healed = lw_tick_heal(s, 1, 200);
    int ok = (healed == 0 && s->entities[1].hp == 700);
    if (!ok) printf("  heal_tick_unhealable: healed=%d -> FAIL\n", healed);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_more_effects_parity:\n");
    int n = 0, ok = 0;
    n++; if (test_shackle_strength())                { printf("   1  shackle_strength OK\n"); ok++; }
    n++; if (test_shackle_negative_magic_clamped())  { printf("   2  shackle_neg_magic_clamp OK\n"); ok++; }
    n++; if (test_vitality())                        { printf("   3  vitality OK\n"); ok++; }
    n++; if (test_nova_vitality())                   { printf("   4  nova_vitality OK\n"); ok++; }
    n++; if (test_raw_heal())                        { printf("   5  raw_heal OK\n"); ok++; }
    n++; if (test_raw_heal_capped())                 { printf("   6  raw_heal_capped OK\n"); ok++; }
    n++; if (test_nova_damage())                     { printf("   7  nova_damage OK\n"); ok++; }
    n++; if (test_nova_damage_capped_at_gap())       { printf("   8  nova_damage_capped OK\n"); ok++; }
    n++; if (test_life_damage_basic())               { printf("   9  life_damage OK\n"); ok++; }
    n++; if (test_life_damage_with_reflect())        { printf("  10  life_damage_reflect OK\n"); ok++; }
    n++; if (test_life_damage_self())                { printf("  11  life_damage_self OK\n"); ok++; }
    n++; if (test_steal_life())                      { printf("  12  steal_life OK\n"); ok++; }
    n++; if (test_steal_life_unhealable())           { printf("  13  steal_life_unhealable OK\n"); ok++; }
    n++; if (test_antidote())                        { printf("  14  antidote OK\n"); ok++; }
    n++; if (test_debuff())                          { printf("  15  debuff OK\n"); ok++; }
    n++; if (test_total_debuff_strips_irreductible()){ printf("  16  total_debuff_irreductible OK\n"); ok++; }
    n++; if (test_aftereffect_tick())                { printf("  17  aftereffect_tick OK\n"); ok++; }
    n++; if (test_heal_tick())                       { printf("  18  heal_tick OK\n"); ok++; }
    n++; if (test_heal_tick_unhealable())            { printf("  19  heal_tick_unhealable OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
