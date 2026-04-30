/*
 * lw_winner.c -- alive-team count + HP tiebreak.
 *
 * The Leek Wars engine supports up to N teams (battle royale = 10).
 * We scan team_ids of alive entities and count distinct values.
 */

#include "lw_winner.h"


/* Up to LW_MAX_ENTITIES distinct team ids (overkill but cheap). */
#define LW_MAX_TEAMS  LW_MAX_ENTITIES


int lw_compute_winner(const LwState *state, int draw_check_hp) {
    if (state == NULL) return LW_WIN_DRAW;

    int alive_count[LW_MAX_TEAMS] = {0};
    long long hp_sum[LW_MAX_TEAMS] = {0};
    int seen_teams[LW_MAX_TEAMS];
    int n_teams_seen = 0;

    /* Walk all entities, counting alive per team_id. */
    for (int i = 0; i < state->n_entities; i++) {
        const LwEntity *e = &state->entities[i];
        int team = e->team_id;
        if (team < 0 || team >= LW_MAX_TEAMS) continue;

        /* Track which teams we've seen (so we can iterate later). */
        int already = 0;
        for (int j = 0; j < n_teams_seen; j++) {
            if (seen_teams[j] == team) { already = 1; break; }
        }
        if (!already) {
            seen_teams[n_teams_seen++] = team;
        }

        if (e->alive) {
            alive_count[team]++;
            hp_sum[team] += e->hp;
        }
    }

    int alive_team_count = 0;
    int last_alive = -1;
    for (int j = 0; j < n_teams_seen; j++) {
        int t = seen_teams[j];
        if (alive_count[t] > 0) {
            alive_team_count++;
            last_alive = t;
        }
    }

    if (alive_team_count == 1) return last_alive;

    /* alive_team_count == 0 (everyone dead) OR > 1 (still fighting). */
    if (alive_team_count == 0) return LW_WIN_DRAW;

    /* > 1 alive: ongoing unless caller asked for a tiebreak. */
    if (!draw_check_hp) return LW_WIN_ONGOING;

    /* Tiebreak by total HP. */
    int best_team = -1;
    long long best_hp = -1;
    int tied = 0;
    for (int j = 0; j < n_teams_seen; j++) {
        int t = seen_teams[j];
        if (alive_count[t] == 0) continue;
        if (hp_sum[t] > best_hp) {
            best_hp = hp_sum[t];
            best_team = t;
            tied = 0;
        } else if (hp_sum[t] == best_hp) {
            tied = 1;
        }
    }
    if (tied) return LW_WIN_DRAW;
    return best_team;
}
