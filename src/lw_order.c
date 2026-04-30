/*
 * lw_order.c -- StartOrder.compute byte-for-byte.
 *
 * The exact draw count and ordering matters for byte-for-byte parity:
 * the Python reference pulls T rng draws (one per team picked) before
 * any other game logic runs. Diverging here would mean every fight's
 * jet rolls land at different RNG state vs. Python.
 */

#include "lw_order.h"
#include "lw_rng.h"
#include <math.h>


/* Slot per team: list of entity indices, sorted by frequency desc. */
typedef struct {
    int team_id;
    int n;
    int entity_ids[LW_MAX_ENTITIES];
} TeamSlot;


/* Insertion sort: keeps order stable for entities with equal frequency,
 * preserving the input-arrival order (matches Python's sort with
 * negative key, which is stable). */
static void sort_team_by_frequency_desc(TeamSlot *t, const LwState *state) {
    for (int i = 1; i < t->n; i++) {
        int key = t->entity_ids[i];
        int kf  = state->entities[key].base_stats[LW_STAT_FREQUENCY]
                + state->entities[key].buff_stats[LW_STAT_FREQUENCY];
        int j = i - 1;
        while (j >= 0) {
            int jf = state->entities[t->entity_ids[j]].base_stats[LW_STAT_FREQUENCY]
                   + state->entities[t->entity_ids[j]].buff_stats[LW_STAT_FREQUENCY];
            if (jf >= kf) break;
            t->entity_ids[j + 1] = t->entity_ids[j];
            j--;
        }
        t->entity_ids[j + 1] = key;
    }
}


void lw_compute_start_order(LwState *state) {
    if (state == NULL) return;

    /* Group entities by team_id. */
    TeamSlot teams[LW_MAX_ENTITIES];
    int n_teams = 0;
    int team_index_by_id[LW_MAX_ENTITIES];
    for (int i = 0; i < LW_MAX_ENTITIES; i++) team_index_by_id[i] = -1;

    int total_entities = 0;
    for (int i = 0; i < state->n_entities; i++) {
        int t = state->entities[i].team_id;
        if (t < 0 || t >= LW_MAX_ENTITIES) continue;
        if (team_index_by_id[t] < 0) {
            team_index_by_id[t] = n_teams;
            teams[n_teams].team_id = t;
            teams[n_teams].n = 0;
            n_teams++;
        }
        int idx = team_index_by_id[t];
        teams[idx].entity_ids[teams[idx].n++] = i;
        total_entities++;
    }

    state->n_in_order = total_entities;
    state->order_index = 0;
    if (n_teams == 0) return;

    /* Sort each team by frequency descending (stable). */
    for (int i = 0; i < n_teams; i++) {
        sort_team_by_frequency_desc(&teams[i], state);
    }

    /* Compute probabilities. probas[i] = 1 / (1 + 10^((sum - f_i) / 100)). */
    double probas[LW_MAX_ENTITIES] = {0};
    int max_freq[LW_MAX_ENTITIES];
    int sum_freq = 0;
    for (int i = 0; i < n_teams; i++) {
        int f = state->entities[teams[i].entity_ids[0]].base_stats[LW_STAT_FREQUENCY]
              + state->entities[teams[i].entity_ids[0]].buff_stats[LW_STAT_FREQUENCY];
        max_freq[i] = f;
        sum_freq += f;
    }
    double psum = 0.0;
    for (int i = 0; i < n_teams; i++) {
        double f = (double)max_freq[i];
        probas[i] = 1.0 / (1.0 + pow(10.0, ((double)sum_freq - f) / 100.0));
        psum += probas[i];
    }
    for (int i = 0; i < n_teams; i++) {
        probas[i] = (psum > 0.0) ? probas[i] / psum : 0.0;
    }
    psum = 1.0;

    /* Pick team order. */
    int team_order[LW_MAX_ENTITIES];
    int n_team_order = 0;

    int remaining[LW_MAX_ENTITIES];
    int n_remaining = n_teams;
    for (int i = 0; i < n_teams; i++) remaining[i] = i;

    for (int t = 0; t < n_teams; t++) {
        double v = lw_rng_double(&state->rng_n);

        int picked_in_remaining = -1;
        for (int i = 0; i < n_remaining; i++) {
            int team = remaining[i];
            double p = probas[team];
            if (v <= p) {
                team_order[n_team_order++] = teams[team].team_id;
                /* shrink remaining */
                for (int k = i; k < n_remaining - 1; k++) {
                    remaining[k] = remaining[k + 1];
                }
                n_remaining--;
                picked_in_remaining = team;
                psum -= p;
                break;
            }
            v -= p;
        }

        /* Re-normalize remaining probabilities. (Python uses Inf when
         * psum hits 0; we just clamp by skipping the divide.) */
        for (int i = 0; i < n_teams; i++) {
            if (psum == 0.0) {
                probas[i] = INFINITY;
            } else {
                probas[i] = probas[i] / psum;
            }
        }
        psum = 1.0;
        (void)picked_in_remaining;
    }

    /* Round-robin entities from each team in the picked order. */
    int order[LW_MAX_ENTITIES];
    int n_order = 0;
    int current_team_i = 0;

    int slots_remaining[LW_MAX_ENTITIES];
    for (int i = 0; i < n_teams; i++) slots_remaining[i] = teams[i].n;

    while (n_order < total_entities) {
        int team_id = team_order[current_team_i];
        int idx = team_index_by_id[team_id];
        if (slots_remaining[idx] > 0) {
            int next_pos = teams[idx].n - slots_remaining[idx];
            order[n_order++] = teams[idx].entity_ids[next_pos];
            slots_remaining[idx]--;
        }
        current_team_i = (current_team_i + 1) % n_teams;
    }

    for (int i = 0; i < n_order; i++) {
        state->initial_order[i] = order[i];
    }
}
