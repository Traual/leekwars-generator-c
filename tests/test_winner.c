/*
 * test_winner.c -- alive-team detection + HP tiebreak.
 */

#include "lw_winner.h"
#include "lw_state.h"
#include <stdio.h>
#include <string.h>


static LwState* make_state(int n_entities) {
    LwState *s = lw_state_alloc();
    s->n_entities = n_entities;
    for (int i = 0; i < n_entities; i++) {
        memset(&s->entities[i], 0, sizeof(LwEntity));
        s->entities[i].id = i;
        s->entities[i].alive = 1;
        s->entities[i].cell_id = -1;
        s->entities[i].hp = 1000;
    }
    return s;
}


static int test_two_teams_alive(void) {
    LwState *s = make_state(2);
    s->entities[0].team_id = 0;
    s->entities[1].team_id = 1;
    int w = lw_compute_winner(s, 0);
    int ok = (w == LW_WIN_ONGOING);
    if (!ok) printf("  two_alive: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_team_0_wins(void) {
    LwState *s = make_state(2);
    s->entities[0].team_id = 0;
    s->entities[1].team_id = 1;
    s->entities[1].alive = 0;
    int w = lw_compute_winner(s, 0);
    int ok = (w == 0);
    if (!ok) printf("  team0: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_team_1_wins(void) {
    LwState *s = make_state(2);
    s->entities[0].team_id = 0; s->entities[0].alive = 0;
    s->entities[1].team_id = 1;
    int w = lw_compute_winner(s, 0);
    int ok = (w == 1);
    if (!ok) printf("  team1: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_draw_all_dead(void) {
    LwState *s = make_state(2);
    s->entities[0].team_id = 0; s->entities[0].alive = 0;
    s->entities[1].team_id = 1; s->entities[1].alive = 0;
    int w = lw_compute_winner(s, 0);
    int ok = (w == LW_WIN_DRAW);
    if (!ok) printf("  all_dead: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_three_teams_one_alive(void) {
    LwState *s = make_state(3);
    s->entities[0].team_id = 0; s->entities[0].alive = 0;
    s->entities[1].team_id = 1;
    s->entities[2].team_id = 2; s->entities[2].alive = 0;
    int w = lw_compute_winner(s, 0);
    int ok = (w == 1);
    if (!ok) printf("  br: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_hp_tiebreak_team_0(void) {
    /* Both alive but team 0 has more HP -> team 0 wins on tiebreak. */
    LwState *s = make_state(2);
    s->entities[0].team_id = 0; s->entities[0].hp = 800;
    s->entities[1].team_id = 1; s->entities[1].hp = 600;
    int w = lw_compute_winner(s, 1);
    int ok = (w == 0);
    if (!ok) printf("  hp_break_t0: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_hp_tiebreak_team_1(void) {
    LwState *s = make_state(2);
    s->entities[0].team_id = 0; s->entities[0].hp = 200;
    s->entities[1].team_id = 1; s->entities[1].hp = 700;
    int w = lw_compute_winner(s, 1);
    int ok = (w == 1);
    if (!ok) printf("  hp_break_t1: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_hp_exact_tie_is_draw(void) {
    LwState *s = make_state(2);
    s->entities[0].team_id = 0; s->entities[0].hp = 500;
    s->entities[1].team_id = 1; s->entities[1].hp = 500;
    int w = lw_compute_winner(s, 1);
    int ok = (w == LW_WIN_DRAW);
    if (!ok) printf("  hp_tie: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


static int test_hp_aggregates_team(void) {
    /* Team 0 has 2 alive (500+500 = 1000); team 1 has 1 alive (800). */
    LwState *s = make_state(3);
    s->entities[0].team_id = 0; s->entities[0].hp = 500;
    s->entities[1].team_id = 0; s->entities[1].hp = 500;
    s->entities[2].team_id = 1; s->entities[2].hp = 800;
    int w = lw_compute_winner(s, 1);
    int ok = (w == 0);
    if (!ok) printf("  hp_aggr: w=%d -> FAIL\n", w);
    lw_state_free(s);
    return ok;
}


int main(void) {
    printf("test_winner:\n");
    int n = 0, ok = 0;
    n++; if (test_two_teams_alive())          { printf("   1  two_teams_alive OK\n"); ok++; }
    n++; if (test_team_0_wins())              { printf("   2  team_0_wins OK\n"); ok++; }
    n++; if (test_team_1_wins())              { printf("   3  team_1_wins OK\n"); ok++; }
    n++; if (test_draw_all_dead())            { printf("   4  draw_all_dead OK\n"); ok++; }
    n++; if (test_three_teams_one_alive())    { printf("   5  battle_royale OK\n"); ok++; }
    n++; if (test_hp_tiebreak_team_0())       { printf("   6  hp_tiebreak_t0 OK\n"); ok++; }
    n++; if (test_hp_tiebreak_team_1())       { printf("   7  hp_tiebreak_t1 OK\n"); ok++; }
    n++; if (test_hp_exact_tie_is_draw())     { printf("   8  hp_exact_tie OK\n"); ok++; }
    n++; if (test_hp_aggregates_team())       { printf("   9  hp_aggregates OK\n"); ok++; }
    printf("\n%d/%d cases passed\n", ok, n);
    return ok == n ? 0 : 1;
}
