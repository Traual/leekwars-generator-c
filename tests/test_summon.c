/*
 * test_summon.c -- bulb registry + summon allocation primitive.
 */

#include "lw_state.h"
#include "lw_summon.h"
#include <stdio.h>
#include <string.h>


static LwState* fresh_state(void) {
    LwState *s = lw_state_alloc();
    s->n_entities = 1;
    LwEntity *c = &s->entities[0];
    memset(c, 0, sizeof(*c));
    c->id = 0; c->fid = 1; c->team_id = 0; c->alive = 1;
    c->level = 150;
    c->cell_id = 5;
    c->hp = 1000; c->total_hp = 1000;
    s->n_in_order = 1;
    s->initial_order[0] = 0;
    s->map.entity_at_cell[5] = 0;
    return s;
}


static int test_register_lookup(void) {
    lw_bulb_clear();
    LwBulbTemplate t = {0};
    t.bulb_id = 1;
    t.min_life = 100; t.max_life = 1000;
    t.min_strength = 50; t.max_strength = 300;
    t.min_tp = 6; t.max_tp = 9;
    t.min_mp = 3; t.max_mp = 5;
    t.n_chips = 0;
    int rc = lw_bulb_register(1, &t);
    int ok = (rc == 0 &&
              lw_bulb_get(1) != NULL &&
              lw_bulb_get(1)->min_life == 100 &&
              lw_bulb_get(99) == NULL);
    if (!ok) printf("  register_lookup: rc=%d -> FAIL\n", rc);
    return ok;
}


static int test_summon_basic(void) {
    /* level 150 -> coeff = 0.5; min..max = 100..1000 -> stat = 100 + floor(450) = 550. */
    lw_bulb_clear();
    LwBulbTemplate t = {0};
    t.bulb_id = 7;
    t.min_life = 100; t.max_life = 1000;
    t.min_strength = 0; t.max_strength = 200;     /* -> 100 */
    t.min_wisdom = 0;   t.max_wisdom = 100;       /* -> 50 */
    t.min_agility = 0;  t.max_agility = 100;
    t.min_resistance = 0; t.max_resistance = 100;
    t.min_science = 0;  t.max_science = 100;
    t.min_magic = 0;    t.max_magic = 100;
    t.min_tp = 4; t.max_tp = 12;                  /* -> 4 + floor(4) = 8 */
    t.min_mp = 2; t.max_mp = 6;                   /* -> 2 + floor(2) = 4 */
    t.n_chips = 1; t.chip_ids[0] = 100;
    lw_bulb_register(7, &t);

    LwState *s = fresh_state();
    /* destination cell 6 (free). */
    int new_idx = lw_apply_summon(s, /*caster*/0, /*dest*/6,
                                   /*bulb*/7, /*level*/150, /*crit*/0);
    int ok = (new_idx == 1 &&
              s->n_entities == 2 &&
              s->entities[1].alive &&
              s->entities[1].cell_id == 6 &&
              s->entities[1].team_id == 0 &&
              s->entities[1].hp == 550 &&
              s->entities[1].total_hp == 550 &&
              s->entities[1].base_stats[LW_STAT_STRENGTH] == 100 &&
              s->entities[1].base_stats[LW_STAT_WISDOM] == 50 &&
              s->entities[1].base_stats[LW_STAT_TP] == 8 &&
              s->entities[1].base_stats[LW_STAT_MP] == 4 &&
              s->entities[1].n_chips == 1 &&
              s->entities[1].chips[0] == 100 &&
              s->map.entity_at_cell[6] == 1);
    if (!ok) {
        printf("  summon_basic: idx=%d hp=%d str=%d wis=%d tp=%d mp=%d -> FAIL\n",
               new_idx, s->entities[1].hp,
               s->entities[1].base_stats[LW_STAT_STRENGTH],
               s->entities[1].base_stats[LW_STAT_WISDOM],
               s->entities[1].base_stats[LW_STAT_TP],
               s->entities[1].base_stats[LW_STAT_MP]);
    }
    lw_state_free(s);
    return ok;
}


static int test_summon_critical_bonus(void) {
    /* critical -> 1.2x multiplier. life base = 550 -> 660. */
    lw_bulb_clear();
    LwBulbTemplate t = {0};
    t.bulb_id = 7;
    t.min_life = 100; t.max_life = 1000;
    lw_bulb_register(7, &t);

    LwState *s = fresh_state();
    int new_idx = lw_apply_summon(s, 0, 6, 7, 150, /*crit*/1);
    int ok = (new_idx == 1 && s->entities[1].hp == 660);
    if (!ok) printf("  summon_critical: hp=%d expected 660 -> FAIL\n",
                    s->entities[1].hp);
    lw_state_free(s);
    return ok;
}


static int test_summon_inserts_into_order_after_caster(void) {
    /* Caster at order pos 0. Summon should go at pos 1. */
    lw_bulb_clear();
    LwBulbTemplate t = {0};
    t.bulb_id = 7;
    t.min_life = 200; t.max_life = 200;
    lw_bulb_register(7, &t);

    LwState *s = fresh_state();
    /* Add a second entity in the original order so we can verify the
     * shift. */
    s->n_entities = 2;
    LwEntity *e2 = &s->entities[1];
    memset(e2, 0, sizeof(*e2));
    e2->id = 1; e2->team_id = 1; e2->alive = 1;
    e2->cell_id = 7; e2->hp = 1000; e2->total_hp = 1000;
    s->n_in_order = 2;
    s->initial_order[0] = 0;
    s->initial_order[1] = 1;
    s->map.entity_at_cell[7] = 1;

    int new_idx = lw_apply_summon(s, /*caster*/0, /*dest*/8,
                                   /*bulb*/7, /*level*/150, /*crit*/0);
    /* Expected order: [0, new_idx, 1]. */
    int ok = (new_idx == 2 &&
              s->n_in_order == 3 &&
              s->initial_order[0] == 0 &&
              s->initial_order[1] == new_idx &&
              s->initial_order[2] == 1);
    if (!ok) printf("  summon_order: order=[%d, %d, %d] new=%d -> FAIL\n",
                    s->initial_order[0], s->initial_order[1],
                    s->initial_order[2], new_idx);
    lw_state_free(s);
    return ok;
}


static int test_summon_rejects_occupied_cell(void) {
    lw_bulb_clear();
    LwBulbTemplate t = {0};
    t.bulb_id = 7;
    t.min_life = 100; t.max_life = 100;
    lw_bulb_register(7, &t);

    LwState *s = fresh_state();
    /* cell 5 is occupied by the caster; summon should be rejected. */
    int new_idx = lw_apply_summon(s, 0, 5, 7, 150, 0);
    int ok = (new_idx == -1 && s->n_entities == 1);
    if (!ok) printf("  summon_rejects: idx=%d n=%d -> FAIL\n",
                    new_idx, s->n_entities);
    lw_state_free(s);
    return ok;
}


static int test_summon_rejects_unknown_template(void) {
    lw_bulb_clear();
    LwState *s = fresh_state();
    int new_idx = lw_apply_summon(s, 0, 6, 99, 150, 0);
    int ok = (new_idx == -1 && s->n_entities == 1);
    if (!ok) printf("  summon_unknown: -> FAIL\n");
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_summon:\n");
    int n = 0, ok = 0;
    n++; if (test_register_lookup())                       { printf("   1  register_lookup OK\n"); ok++; }
    n++; if (test_summon_basic())                          { printf("   2  summon_basic OK\n"); ok++; }
    n++; if (test_summon_critical_bonus())                 { printf("   3  summon_critical_bonus OK\n"); ok++; }
    n++; if (test_summon_inserts_into_order_after_caster()){ printf("   4  summon_order OK\n"); ok++; }
    n++; if (test_summon_rejects_occupied_cell())          { printf("   5  summon_rejects_occupied OK\n"); ok++; }
    n++; if (test_summon_rejects_unknown_template())       { printf("   6  summon_rejects_unknown OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
