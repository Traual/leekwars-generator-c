/*
 * lw_start_order.c -- 1:1 port of state/StartOrder.java
 *
 * RNG draws here MUST occur in the same order as the Java source.
 * One getDouble() per team to determine the team-pick order.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/StartOrder.java
 */
#include "lw_start_order.h"

#include "lw_rng.h"

#include <math.h>
#include <stddef.h>


/* Forward decls -- defined elsewhere. */
struct LwEntity;
struct LwState;
int       lw_entity_get_team     (const struct LwEntity *e);
int       lw_entity_get_frequency(const struct LwEntity *e);
uint64_t* lw_state_get_random    (struct LwState *state);


/* Implicit Java default ctor: empty lists, totalEntities = 0. */
void lw_start_order_init(LwStartOrder *self) {
    self->n_teams        = 0;
    self->total_entities = 0;
    for (int i = 0; i < LW_START_ORDER_MAX_TEAMS; i++) {
        self->n_per_team[i] = 0;
    }
}


/* Java:
 *   public void addEntity(Entity entity) {
 *       while (teams.size() < entity.getTeam() + 1) {
 *           teams.add(new ArrayList<Entity>());
 *       }
 *       teams.get(entity.getTeam()).add(entity);
 *       totalEntities++;
 *   }
 */
void lw_start_order_add_entity(LwStartOrder *self, struct LwEntity *entity) {
    int team = lw_entity_get_team(entity);
    while (self->n_teams < team + 1) {
        self->n_per_team[self->n_teams] = 0;
        self->n_teams++;
    }
    if (self->n_per_team[team] < LW_START_ORDER_MAX_PER_TEAM) {
        self->teams[team][self->n_per_team[team]++] = entity;
    }
    self->total_entities++;
}


/* Java's Comparator: Integer.signum(e2.getFrequency() - e1.getFrequency())
 * (frequency descending). Stable sort -- Java Collections.sort uses
 * mergesort, which is stable. We keep the same property by using a
 * straightforward stable insertion sort. */
static void lw_start_order_sort_team(struct LwEntity **arr, int n) {
    for (int i = 1; i < n; i++) {
        struct LwEntity *cur = arr[i];
        int cur_freq = lw_entity_get_frequency(cur);
        int j = i;
        while (j > 0) {
            int prev_freq = lw_entity_get_frequency(arr[j - 1]);
            /* signum(e2.freq - e1.freq) > 0 means cur should come before arr[j-1]
             * Comparator returns positive when e1 > e2, but the second-arg-first
             * subtraction (e2 - e1) means: positive => e2.freq > e1.freq, i.e.
             * cur (e1) should be AFTER arr[j-1] (e2)? Let's recheck:
             *
             *   compare(e1, e2) = signum(e2.freq - e1.freq)
             *
             * If e2.freq > e1.freq, compare > 0 => e1 considered "greater"
             * => e1 sorted AFTER e2 => high frequency first.
             *
             * For an insertion of cur at position j, swap while
             *   compare(arr[j-1], cur) > 0   means arr[j-1] > cur
             *   i.e. cur.freq > arr[j-1].freq
             */
            if (cur_freq > prev_freq) {
                arr[j] = arr[j - 1];
                j--;
            } else {
                break;
            }
        }
        arr[j] = cur;
    }
}


/* Java:
 *   public List<Entity> compute(State state) {
 *
 *       // Sort entities inside team on their frequency
 *       for (List<Entity> team : teams) {
 *           Collections.sort(team, new Comparator<Entity>() {
 *               public int compare(Entity e1, Entity e2) {
 *                   return Integer.signum(e2.getFrequency() - e1.getFrequency());
 *               }
 *           });
 *       }
 *
 *       // Compute probability for each team, example : [0.15, 0.35, 0.5]
 *       List<Double> probas = new ArrayList<Double>();
 *       List<Integer> frequencies = new ArrayList<Integer>();
 *
 *       double sum = 0;
 *       for (int i = 0; i < teams.size(); ++i) {
 *           int frequency = teams.get(i).get(0).getFrequency();
 *           frequencies.add(frequency);
 *           sum += frequency;
 *       }
 *
 *       double psum = 0;
 *       for (int i = 0; i < teams.size(); ++i) {
 *
 *           double f = frequencies.get(i);
 *           double p = 1d / (1d + Math.pow(10, (sum - f) / 100d));
 *
 *           probas.add(p);
 *           psum += p;
 *       }
 *
 *       for (int i = 0; i < teams.size(); ++i) {
 *           probas.set(i, probas.get(i) / psum);
 *       }
 *       psum = 1;
 *
 *       // Compute team order, example : [team3, team1, team2]
 *       List<Integer> teamOrder = new ArrayList<Integer>();
 *       List<Integer> remaining = new ArrayList<Integer>();
 *       for (int i = 0; i < teams.size(); ++i) {
 *           remaining.add(i);
 *       }
 *
 *       for (int t = 0; t < teams.size(); ++t) {
 *
 *           double v = state.getRandom().getDouble();
 *
 *           for (int i = 0; i < remaining.size(); ++i) {
 *
 *               int team = remaining.get(i);
 *               double p = probas.get(team);
 *
 *               if (v <= p) {
 *                   teamOrder.add(team);
 *                   remaining.remove(i);
 *                   psum -= p;
 *                   break;
 *               }
 *               v -= p;
 *           }
 *
 *           for (int i = 0; i < teams.size(); ++i) {
 *               probas.set(i, probas.get(i) / psum);
 *           }
 *           psum = 1;
 *       }
 *
 *       // Compute entity order : [entity5, entity1, entity2, entity4, ...]
 *       List<Entity> order = new ArrayList<Entity>();
 *
 *       int currentTeamI = 0;
 *       while (order.size() != totalEntities) {
 *
 *           int team = teamOrder.get(currentTeamI);
 *           if (teams.get(team).size() > 0) {
 *               order.add(teams.get(team).remove(0));
 *           }
 *
 *           currentTeamI = (currentTeamI + 1) % teams.size();
 *       }
 *
 *       return order;
 *   }
 */
int lw_start_order_compute(LwStartOrder *self, struct LwState *state,
                           struct LwEntity **out_buf, int out_cap) {

    // Sort entities inside team on their frequency
    for (int i = 0; i < self->n_teams; i++) {
        lw_start_order_sort_team(self->teams[i], self->n_per_team[i]);
    }

    // Compute probability for each team, example : [0.15, 0.35, 0.5]
    double probas[LW_START_ORDER_MAX_TEAMS];
    int    frequencies[LW_START_ORDER_MAX_TEAMS];

    double sum = 0;
    for (int i = 0; i < self->n_teams; i++) {
        int frequency = lw_entity_get_frequency(self->teams[i][0]);
        frequencies[i] = frequency;
        sum += frequency;
    }

    double psum = 0;
    for (int i = 0; i < self->n_teams; i++) {
        double f = (double)frequencies[i];
        double p = 1.0 / (1.0 + pow(10.0, (sum - f) / 100.0));
        probas[i] = p;
        psum += p;
    }

    for (int i = 0; i < self->n_teams; i++) {
        probas[i] = probas[i] / psum;
    }
    psum = 1;

    // Compute team order, example : [team3, team1, team2]
    int teamOrder[LW_START_ORDER_MAX_TEAMS];
    int n_team_order = 0;
    int remaining[LW_START_ORDER_MAX_TEAMS];
    int n_remaining = 0;
    for (int i = 0; i < self->n_teams; i++) {
        remaining[n_remaining++] = i;
    }

    uint64_t *rng = lw_state_get_random(state);

    for (int t = 0; t < self->n_teams; t++) {

        double v = lw_rng_get_double(rng);

        for (int i = 0; i < n_remaining; i++) {

            int team = remaining[i];
            double p = probas[team];

            if (v <= p) {
                teamOrder[n_team_order++] = team;
                /* remaining.remove(i): shift down */
                for (int k = i; k + 1 < n_remaining; k++) {
                    remaining[k] = remaining[k + 1];
                }
                n_remaining--;
                psum -= p;
                break;
            }
            v -= p;
        }

        for (int i = 0; i < self->n_teams; i++) {
            probas[i] = probas[i] / psum;
        }
        psum = 1;
    }

    // Compute entity order : [entity5, entity1, entity2, entity4, ...]
    int n_order = 0;

    /* Local cursors into each team list (we don't want to mutate
     * self->teams[] state because StartOrder is single-use anyway, but
     * Java does .remove(0) -- mirror that with a head-index per team). */
    int head[LW_START_ORDER_MAX_TEAMS];
    for (int i = 0; i < self->n_teams; i++) head[i] = 0;

    int currentTeamI = 0;
    while (n_order != self->total_entities) {

        int team = teamOrder[currentTeamI];
        if (head[team] < self->n_per_team[team]) {
            if (n_order < out_cap) {
                out_buf[n_order++] = self->teams[team][head[team]];
            } else {
                n_order++;
            }
            head[team]++;
        }

        if (self->n_teams > 0) {
            currentTeamI = (currentTeamI + 1) % self->n_teams;
        } else {
            break;
        }
    }

    return n_order;
}
