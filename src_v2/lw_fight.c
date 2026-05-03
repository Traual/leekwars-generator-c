/*
 * lw_fight.c -- 1:1 port of fight/Fight.java
 *
 * The turn loop is the engine's hot path. EVERY state mutation /
 * action stream emit / RNG draw must happen in the same order as the
 * Java source -- the parity test compares full action streams.
 *
 * The fight types (TYPE_SOLO_GARDEN, etc.) are the public enum values
 * the binding feeds in via Scenario.type. The fight CONTEXTs are
 * derived in getFightContext().
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/fight/Fight.java
 */
#include "lw_fight.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "lw_constants.h"
#include "lw_actions.h"
#include "lw_rng.h"


/* ---- Forward declarations into the State / Entity / Order / Team /
 *      Statistics / EntityAI / FightListener / FarmerLog modules. These
 *      mirror the Java method signatures that Fight.java calls; the
 *      bodies live in their own .c files. */

struct LwState;
struct LwOrder;
struct LwEntity;
struct LwTeam;
struct LwActions;
struct LwStatisticsManager;
struct LwLeekLog;
struct LwAttack;
struct LwEffect;
struct LwEntityAI;
struct LwBulbAI;

/* State helpers (lw_state.c) */
struct LwOrder*   lw_state_get_order        (struct LwState *state);
int               lw_state_get_state        (const struct LwState *state);
void              lw_state_set_state        (struct LwState *state, int s);
int               lw_state_get_type         (const struct LwState *state);
int               lw_state_get_context     (const struct LwState *state);
int64_t           lw_state_get_seed         (const struct LwState *state);
struct LwActions* lw_state_get_actions      (struct LwState *state);
struct LwStatisticsManager* lw_state_get_statistics(struct LwState *state);
void              lw_state_init_fight       (struct LwState *state);  /* state.init() */
struct LwEntity*  lw_state_get_entity_by_id (struct LwState *state, int64_t id);
int               lw_state_n_entities       (const struct LwState *state);
struct LwEntity*  lw_state_get_entity_at    (struct LwState *state, int idx);
int               lw_state_n_teams          (const struct LwState *state);
struct LwTeam*    lw_state_get_team_at      (struct LwState *state, int idx);
struct LwEntity*  lw_state_get_last_entity  (struct LwState *state);
int               lw_state_get_all_entities (struct LwState *state, int include_dead, struct LwEntity **out, int cap);
void              lw_state_remove_invocation(struct LwState *state, struct LwEntity *e, int log);
void              lw_state_end_turn         (struct LwState *state);
int               lw_state_use_chip         (struct LwState *state, struct LwEntity *caster, struct LwCell *target, struct LwChip *template_);
int               lw_state_summon_entity_named(struct LwState *state, struct LwEntity *caster, struct LwCell *target, struct LwChip *template_, const char *name);
uint64_t*         lw_state_get_random       (struct LwState *state); /* returns &state->rng_n */

/* Order helpers (lw_order.c) */
int               lw_order_get_turn   (const struct LwOrder *order);
struct LwEntity*  lw_order_current    (struct LwOrder *order);

/* Team helpers (lw_team.c) */
int               lw_team_get_id          (const struct LwTeam *team);
void              lw_team_set_id          (struct LwTeam *team, int id);
void              lw_team_add_flag        (struct LwTeam *team, int flag);
int               lw_team_size            (const struct LwTeam *team);
int               lw_team_is_alive        (const struct LwTeam *team);
int               lw_team_is_dead         (const struct LwTeam *team);
int               lw_team_contains_chest  (const struct LwTeam *team);
int               lw_team_get_life        (const struct LwTeam *team);

/* Entity helpers (lw_entity.c) */
int               lw_entity_is_dead       (const struct LwEntity *entity);
int               lw_entity_is_summon     (const struct LwEntity *entity);
int               lw_entity_get_type      (const struct LwEntity *entity);
int               lw_entity_get_agility   (const struct LwEntity *entity);
void              lw_entity_start_fight   (struct LwEntity *entity);
void              lw_entity_start_turn    (struct LwEntity *entity);
void              lw_entity_end_turn      (struct LwEntity *entity);
void*             lw_entity_get_ai_file   (const struct LwEntity *entity);
void              lw_entity_add_operations(struct LwEntity *entity, int64_t ops);
void              lw_entity_set_birth_turn(struct LwEntity *entity, int turn);
void              lw_entity_set_fight     (struct LwEntity *entity, void *fight);
void              lw_entity_set_ai        (struct LwEntity *entity, void *ai);
void*             lw_entity_get_ai        (const struct LwEntity *entity);
void*             lw_entity_get_logs      (const struct LwEntity *entity);

/* StatisticsManager helpers (lw_statistics.c).
 *
 * The Java engine calls statistics.init / .characteristics / .addTimes /
 * .error / .endFight. The vtable struct (LwStatisticsManager) is in
 * lw_statistics.h; these wrappers null-check and dispatch. */
void lw_stats_init_entity         (struct LwStatisticsManager *st, struct LwEntity *entity);
void lw_stats_characteristics     (struct LwStatisticsManager *st, struct LwEntity *entity);
void lw_stats_add_times           (struct LwStatisticsManager *st, struct LwEntity *entity, int64_t nanos, int64_t operations);
void lw_stats_error               (struct LwStatisticsManager *st, struct LwEntity *entity);
void lw_stats_end_fight           (struct LwStatisticsManager *st, struct LwEntity **values, int n_values);
void lw_actions_add_ops_and_times(struct LwActions *actions, struct LwStatisticsManager *st);

/* High-resolution timer (binding-supplied, mirrors Java System.nanoTime). */
int64_t lw_now_nanos(void);

/* Listener hook (lw_fight_listener.c) -- newTurn(this) */
void lw_fight_listener_new_turn(struct LwFightListener *listener, LwFight *fight);

/* Chip / Attack helpers -- canonical defs in headers. */
#include "lw_chip.h"
#include "lw_attack.h"


/* ---- Logger stubs (Java's Log.i(TAG, msg)). No-op in C; the binding
 *      can install a hook if needed. */
static void lw_log_i(const char *tag, const char *msg) {
    (void)tag;
    (void)msg;
}


/* ----------------------------------------------------------------- */
/* Constructors                                                       */
/* ----------------------------------------------------------------- */

/* Java: public Fight(Generator generator) { this(generator, null); } */
void lw_fight_init(LwFight *self, struct LwGenerator *generator) {
    lw_fight_init_with_listener(self, generator, NULL);
}

/* Java: public Fight(Generator generator, FightListener listener) {
 *           this.generator = generator;
 *           this.listener = listener;
 *       }
 */
void lw_fight_init_with_listener(LwFight *self, struct LwGenerator *generator,
                                 struct LwFightListener *listener) {
    if (self == NULL) return;
    self->m_winteam = -1;             /* private int mWinteam = -1; */
    self->generator = generator;
    self->m_id = 0;
    self->m_boss = 0;
    self->m_start_farmer = -1;        /* private int mStartFarmer = -1; */
    self->max_turns = LW_FIGHT_MAX_TURNS;
    self->execution_time = 0;         /* public long executionTime = 0; */
    self->listener = listener;
    self->state = NULL;
    self->ai_dispatch = NULL;
    self->ai_dispatch_userdata = NULL;
}

void lw_fight_set_ai_dispatch(LwFight *self, lw_fight_ai_dispatch_t fn, void *userdata) {
    if (self == NULL) return;
    self->ai_dispatch = fn;
    self->ai_dispatch_userdata = userdata;
}

void lw_fight_set_state(LwFight *self, struct LwState *state) {
    if (self == NULL) return;
    self->state = state;
}


/* ----------------------------------------------------------------- */
/* Trivial accessors (Java getters/setters one-for-one)               */
/* ----------------------------------------------------------------- */

/* Java: public void addFlag(int team, int flag) {
 *           state.getTeams().get(team).addFlag(flag);
 *       } */
void lw_fight_add_flag(LwFight *self, int team, int flag) {
    if (self == NULL || self->state == NULL) return;
    if (team < 0 || team >= lw_state_n_teams(self->state)) return;
    lw_team_add_flag(lw_state_get_team_at(self->state, team), flag);
}

/* Java: public int getWinner() { return mWinteam; } */
int lw_fight_get_winner(const LwFight *self) {
    return self->m_winteam;
}

/* Java: public void setTeamID(int team, int id) {
 *           if (team < state.getTeams().size()) {
 *               state.getTeams().get(team).setID(id);
 *           }
 *       } */
void lw_fight_set_team_id(LwFight *self, int team, int id) {
    if (self == NULL || self->state == NULL) return;
    if (team < lw_state_n_teams(self->state)) {
        lw_team_set_id(lw_state_get_team_at(self->state, team), id);
    }
}

/* Java: public void setStartFarmer(int startFarmer) { mStartFarmer = startFarmer; } */
void lw_fight_set_start_farmer(LwFight *self, int start_farmer) {
    self->m_start_farmer = start_farmer;
}

/* Java: public int getStartFarmer() { return mStartFarmer; } */
int lw_fight_get_start_farmer(const LwFight *self) {
    return self->m_start_farmer;
}

/* Java: public int getTeamID(int team) {
 *           if (team < state.getTeams().size()) {
 *               return state.getTeams().get(team).getID();
 *           }
 *           return -1;
 *       } */
int lw_fight_get_team_id(const LwFight *self, int team) {
    if (self == NULL || self->state == NULL) return -1;
    if (team < lw_state_n_teams((struct LwState*)self->state)) {
        return lw_team_get_id(lw_state_get_team_at((struct LwState*)self->state, team));
    }
    return -1;
}

/* Java: public int getId() { return mId; } */
int lw_fight_get_id(const LwFight *self) { return self->m_id; }
/* Java: public int getBoss() { return mBoss; } */
int lw_fight_get_boss(const LwFight *self) { return self->m_boss; }
/* Java: public void setId(int f) { mId = f; } */
void lw_fight_set_id(LwFight *self, int id) { self->m_id = id; }
/* Java: public void setBoss(int b) { mBoss = b; } */
void lw_fight_set_boss(LwFight *self, int boss) { self->m_boss = boss; }
/* Java: public void setMaxTurns(int max_turns) { this.max_turns = max_turns; } */
void lw_fight_set_max_turns(LwFight *self, int max_turns) { self->max_turns = max_turns; }
/* Java: public int getDuration() { return state.getOrder().getTurn(); } */
int lw_fight_get_duration(const LwFight *self) {
    return lw_order_get_turn(lw_state_get_order((struct LwState*)self->state));
}
/* Java: public int getTurn() { return state.getOrder().getTurn(); } */
int lw_fight_get_turn(const LwFight *self) {
    return lw_order_get_turn(lw_state_get_order((struct LwState*)self->state));
}
/* Java: public Order getOrder() { return state.getOrder(); } */
struct LwOrder* lw_fight_get_order(LwFight *self) {
    return lw_state_get_order(self->state);
}
/* Java: public State getState() { return this.state; } */
struct LwState* lw_fight_get_state(LwFight *self) { return self->state; }
/* Java: public Entity getEntity(long id) { return state.getEntity(id); } */
struct LwEntity* lw_fight_get_entity(LwFight *self, int64_t id) {
    return lw_state_get_entity_by_id(self->state, id);
}


/* ----------------------------------------------------------------- */
/* canStartFight                                                      */
/* ----------------------------------------------------------------- */

/* Java: private boolean canStartFight() {
 *           if (state.getTeams().size() < 2) return false;
 *           return true;
 *       }
 */
static int lw_fight_can_start_fight(const LwFight *self) {
    if (lw_state_n_teams(self->state) < 2) return 0;
    return 1;
}


/* ----------------------------------------------------------------- */
/* startFight                                                         */
/* ----------------------------------------------------------------- */

/* Java: public void startFight(boolean drawCheckLife) throws Exception
 *
 * Returns 0 on success, non-zero on FightException (mapped from
 * initFight). The throw point is replaced by an early return; downstream
 * the caller (Generator.runScenario) translates the status into
 * outcome.exception. */
int lw_fight_start_fight(LwFight *self, int draw_check_life) {
    if (self == NULL || self->state == NULL) return -1;
    int rc = lw_fight_init_fight(self);
    if (rc != 0) return rc;

    /* for (var entity : state.getEntities().values()) {
     *     // Build AI after the fight is ready (static init)
     *     var ai = EntityAI.build(this.generator, (AIFile) entity.getAIFile(), entity);
     *     entity.setAI(ai);
     *     ((EntityAI) ai).setFight(this);
     *     ((EntityAI) ai).init();
     *     ((EntityAI) ai).getRandom().seed(state.getSeed());
     *
     *     // Check all entities characteristics
     *     state.statistics.init(entity);
     *     state.statistics.characteristics(entity);
     *
     *     // Start fight for entity
     *     entity.startFight();
     * }
     *
     * NOTE: in C, AI build/seed/init is owned by the binding (the
     * Python AI dispatcher). We still iterate and dispatch the
     * statistics + entity.startFight() calls in the same order.
     */
    int n = lw_state_n_entities(self->state);
    for (int i = 0; i < n; i++) {
        struct LwEntity *entity = lw_state_get_entity_at(self->state, i);

        /* AI build is stubbed: the binding installed ai_file +
         * lw_fight_set_ai_dispatch. Java's seeding of the per-AI
         * Random is handled by the binding using state.seed at
         * dispatch time. */

        /* state.statistics.init(entity);
         * state.statistics.characteristics(entity); */
        struct LwStatisticsManager *stats = lw_state_get_statistics(self->state);
        lw_stats_init_entity(stats, entity);
        lw_stats_characteristics(stats, entity);

        /* entity.startFight(); */
        lw_entity_start_fight(entity);
    }

    /* Log.i(TAG, "Turn 1"); */
    lw_log_i("Fight", "Turn 1");

    /* On lance les tours
     * while (state.getOrder().getTurn() <= max_turns
     *        && state.getState() == State.STATE_RUNNING) {
     *     startTurn();
     *     if (state.getOrder().current() == null) {
     *         finishFight();
     *         break;
     *     }
     * }
     */
    while (lw_order_get_turn(lw_state_get_order(self->state)) <= self->max_turns
        && lw_state_get_state(self->state) == LW_FIGHT_STATE_RUNNING) {

        rc = lw_fight_start_turn(self);
        if (rc != 0) return rc;

        if (lw_order_current(lw_state_get_order(self->state)) == NULL) {
            lw_fight_finish_fight(self);
            break;
        }
    }

    /* Si match nul
     * if (state.getOrder().getTurn() == Fight.MAX_TURNS + 1) {
     *     finishFight();
     * }
     */
    if (lw_order_get_turn(lw_state_get_order(self->state))
            == LW_FIGHT_MAX_TURNS + 1) {
        lw_fight_finish_fight(self);
    }

    /* On supprime toutes les invocations
     * var entities = state.getAllEntities(true);
     * for (Entity e : entities) {
     *     if (e.isSummon())
     *         state.removeInvocation(e, true);
     * }
     */
    {
        /* Bound the snapshot to a generous local buffer; the engine never
         * exceeds a few dozen entities per fight. */
        struct LwEntity *entities[256];
        int n_entities = lw_state_get_all_entities(self->state, 1, entities, 256);
        for (int i = 0; i < n_entities; i++) {
            struct LwEntity *e = entities[i];
            if (lw_entity_is_summon(e)) {
                lw_state_remove_invocation(self->state, e, 1);
            }
        }
    }

    /* Calcul de l'équipe gagnante
     * computeWinner(drawCheckLife); */
    lw_fight_compute_winner(self, draw_check_life);

    /* state.statistics.endFight(state.getEntities().values()); */
    {
        struct LwEntity *values[256];
        int n_values = lw_state_n_entities(self->state);
        if (n_values > 256) n_values = 256;
        for (int i = 0; i < n_values; i++) {
            values[i] = lw_state_get_entity_at(self->state, i);
        }
        lw_stats_end_fight(lw_state_get_statistics(self->state), values, n_values);
    }

    /* if (listener != null) listener.newTurn(this); */
    if (self->listener != NULL) {
        lw_fight_listener_new_turn(self->listener, self);
    }

    /* state.getActions().addOpsAndTimes(state.statistics); */
    lw_actions_add_ops_and_times(lw_state_get_actions(self->state),
                                 lw_state_get_statistics(self->state));

    return 0;
}


/* ----------------------------------------------------------------- */
/* computeWinner                                                      */
/* ----------------------------------------------------------------- */

/* Java: public void computeWinner(boolean drawCheckLife) {
 *   mWinteam = -1;
 *   if (state.getType() == State.TYPE_CHEST_HUNT) {
 *       boolean chestsAlive = false;
 *       for (var team : state.getTeams()) {
 *           if (team.containsChest() && team.isAlive()) { chestsAlive = true; break; }
 *       }
 *       if (!chestsAlive) {
 *           mWinteam = -2; // Special: all alive players win
 *       }
 *       return;
 *   }
 *   int alive = 0;
 *   for (int t = 0; t < state.getTeams().size(); ++t) {
 *       if (!state.getTeams().get(t).isDead() && !state.getTeams().get(t).containsChest()) {
 *           alive++;
 *           mWinteam = t;
 *       }
 *   }
 *   if (alive != 1) mWinteam = -1;
 *   if (mWinteam == -1 && drawCheckLife) {
 *       if (state.getTeams().get(0).getLife() > state.getTeams().get(1).getLife())      mWinteam = 0;
 *       else if (state.getTeams().get(1).getLife() > state.getTeams().get(0).getLife()) mWinteam = 1;
 *   }
 * }
 */
void lw_fight_compute_winner(LwFight *self, int draw_check_life) {
    self->m_winteam = -1;

    if (lw_state_get_type(self->state) == LW_FIGHT_TYPE_CHEST_HUNT) {
        int chests_alive = 0;
        int n_teams = lw_state_n_teams(self->state);
        for (int t = 0; t < n_teams; t++) {
            struct LwTeam *team = lw_state_get_team_at(self->state, t);
            if (lw_team_contains_chest(team) && lw_team_is_alive(team)) {
                chests_alive = 1;
                break;
            }
        }
        if (!chests_alive) {
            self->m_winteam = -2;  /* Special: all alive players win */
        }
        return;
    }

    int alive = 0;
    int n_teams = lw_state_n_teams(self->state);
    for (int t = 0; t < n_teams; t++) {
        struct LwTeam *team = lw_state_get_team_at(self->state, t);
        if (!lw_team_is_dead(team) && !lw_team_contains_chest(team)) {
            alive++;
            self->m_winteam = t;
        }
    }
    if (alive != 1) {
        self->m_winteam = -1;
    }

    /* Égalité : on regarde la vie des équipes */
    if (self->m_winteam == -1 && draw_check_life) {
        struct LwTeam *t0 = lw_state_get_team_at(self->state, 0);
        struct LwTeam *t1 = lw_state_get_team_at(self->state, 1);
        if (lw_team_get_life(t0) > lw_team_get_life(t1)) {
            self->m_winteam = 0;
        } else if (lw_team_get_life(t1) > lw_team_get_life(t0)) {
            self->m_winteam = 1;
        }
    }
}


/* ----------------------------------------------------------------- */
/* initFight                                                          */
/* ----------------------------------------------------------------- */

/* Java: public void initFight() throws FightException {
 *   if (state.getTeams().size() < 2)
 *     throw new FightException(FightException.NOT_ENOUGHT_PLAYERS);
 *   if (state.getTeams().get(0).size() == 0 || state.getTeams().get(1).size() == 0) {
 *     if (state.getContext() == Fight.CONTEXT_TOURNAMENT) {
 *       if (state.getTeams().get(0).size() == 0) mWinteam = 1;
 *       else                                     mWinteam = 0;
 *     }
 *     throw new FightException(FightException.NOT_ENOUGHT_PLAYERS);
 *   }
 *   if (!canStartFight())
 *     throw new FightException(FightException.CANT_START_FIGHT);
 *   this.state.init();
 * }
 *
 * Returns 0 on success; non-zero error code on FightException. */
int lw_fight_init_fight(LwFight *self) {
    if (lw_state_n_teams(self->state) < 2) {
        /* throw NOT_ENOUGHT_PLAYERS */
        return 1;
    }
    struct LwTeam *t0 = lw_state_get_team_at(self->state, 0);
    struct LwTeam *t1 = lw_state_get_team_at(self->state, 1);
    if (lw_team_size(t0) == 0 || lw_team_size(t1) == 0) {
        if (lw_state_get_context(self->state) == LW_CONTEXT_TOURNAMENT) {
            /* Si c'est un tournoi faut un gagnant */
            if (lw_team_size(t0) == 0) {
                self->m_winteam = 1;
            } else {
                self->m_winteam = 0;
            }
        }
        return 1;  /* throw NOT_ENOUGHT_PLAYERS */
    }
    if (!lw_fight_can_start_fight(self)) {
        return 2;  /* throw CANT_START_FIGHT */
    }

    /* Init the state */
    lw_state_init_fight(self->state);
    return 0;
}


/* ----------------------------------------------------------------- */
/* finishFight                                                        */
/* ----------------------------------------------------------------- */

/* Java: public void finishFight() { state.setState(State.STATE_FINISHED); } */
void lw_fight_finish_fight(LwFight *self) {
    lw_state_set_state(self->state, LW_FIGHT_STATE_FINISHED);
}


/* ----------------------------------------------------------------- */
/* startTurn -- THE main turn loop                                     */
/* ----------------------------------------------------------------- */

/* Java: public void startTurn() throws Exception {
 *   var current = state.getOrder().current();
 *   if (current == null) return;
 *
 *   state.getActions().log(new ActionEntityTurn(current));
 *   if (listener != null) listener.newTurn(this);
 *
 *   current.startTurn();
 *
 *   if (!current.isDead()) {
 *     var ai = (EntityAI) current.getAI();
 *     if (ai != null) {
 *       if (ai.isValid()) {
 *         ai.setEntity(current);
 *         long startTime = System.nanoTime();
 *         ai.runTurn(state.getOrder().getTurn());
 *         long endTime = System.nanoTime();
 *         state.statistics.addTimes(current, endTime - startTime, ai.operations());
 *         executionTime += endTime - startTime;
 *         current.addOperations(ai.operations());
 *       } else {
 *         if (getTurn() == 1) {
 *           ((LeekLog) current.getLogs()).addSystemLog(LeekLog.SERROR, Error.INVALID_AI, new String[]{"Invalid AI"});
 *         }
 *         log(new ActionAIError(current));
 *         state.statistics.error(current);
 *       }
 *     } else {
 *       // Pas d'IA équipée : juste un warning
 *       ((LeekLog) current.getLogs()).addSystemLog(LeekLog.SWARNING, Error.NO_AI_EQUIPPED);
 *     }
 *     current.endTurn();
 *     state.getActions().log(new ActionEndTurn(current));
 *   }
 *   state.endTurn();
 * }
 */
int lw_fight_start_turn(LwFight *self) {
    struct LwOrder *order = lw_state_get_order(self->state);
    struct LwEntity *current = lw_order_current(order);
    if (current == NULL) {
        return 0;
    }

    /* state.getActions().log(new ActionEntityTurn(current)); */
    lw_actions_log_entity_turn(lw_state_get_actions(self->state), current);

    /* if (listener != null) listener.newTurn(this); */
    if (self->listener != NULL) {
        lw_fight_listener_new_turn(self->listener, self);
    }

    /* Log.i(TAG, "Start turn of " + current.getName()); -- commented out in Java */

    /* current.startTurn(); */
    lw_entity_start_turn(current);

    if (!lw_entity_is_dead(current)) {
        void *ai = lw_entity_get_ai(current);
        if (ai != NULL) {
            /* In Java: ai.isValid() checks the leekscript compiler result.
             * In C the binding installs ai_dispatch only for valid AIs;
             * absence of ai_dispatch implies "no AI" path below. We treat
             * "ai != NULL but ai_dispatch == NULL" as an invalid AI. */
            if (self->ai_dispatch != NULL) {
                /* ai.setEntity(current);  -- handled by the dispatcher */
                int64_t start_time = lw_now_nanos();
                int operations = self->ai_dispatch(self, current,
                                                   lw_entity_get_ai_file(current),
                                                   lw_order_get_turn(order),
                                                   self->ai_dispatch_userdata);
                int64_t end_time = lw_now_nanos();

                if (operations < 0) {
                    /* AI runtime error: same handling as ai.isValid() == false. */
                    if (lw_fight_get_turn(self) == 1) {
                        /* leekLog.addSystemLog(SERROR, INVALID_AI, ["Invalid AI"]) */
                        /* (no-op stub: binding handles per-farmer logs) */
                        (void)lw_entity_get_logs(current);
                    }
                    lw_actions_log_ai_error(lw_state_get_actions(self->state), current);
                    lw_stats_error(lw_state_get_statistics(self->state), current);
                } else {
                    /* state.statistics.addTimes(current, endTime - startTime, ai.operations()); */
                    lw_stats_add_times(lw_state_get_statistics(self->state),
                                       current, end_time - start_time, operations);
                    /* executionTime += endTime - startTime; */
                    self->execution_time += end_time - start_time;
                    /* current.addOperations(ai.operations()); */
                    lw_entity_add_operations(current, operations);
                }
            } else {
                /* Add 'crash' action if AI is invalid */
                if (lw_fight_get_turn(self) == 1) {
                    /* leekLog.addSystemLog(SERROR, INVALID_AI, ["Invalid AI"]) -- stub */
                    (void)lw_entity_get_logs(current);
                }
                lw_actions_log_ai_error(lw_state_get_actions(self->state), current);
                lw_stats_error(lw_state_get_statistics(self->state), current);
            }
        } else {
            /* Pas d'IA équipée : juste un warning */
            /* leekLog.addSystemLog(SWARNING, NO_AI_EQUIPPED) -- stub */
            (void)lw_entity_get_logs(current);
        }
        /* current.endTurn(); */
        lw_entity_end_turn(current);
        /* state.getActions().log(new ActionEndTurn(current)); */
        lw_actions_log_end_turn(lw_state_get_actions(self->state), current);
    }
    /* state.endTurn(); */
    lw_state_end_turn(self->state);

    return 0;
}


/* ----------------------------------------------------------------- */
/* useChip                                                            */
/* ----------------------------------------------------------------- */

/* Java: public int useChip(Entity caster, Cell target, Chip template) {
 *   // Invocation mais sans IA
 *   if (template.getAttack().getEffectParametersByType(Effect.TYPE_SUMMON) != null) {
 *     ((EntityAI) caster.getAI()).addSystemLog(LeekLog.WARNING, FarmerLog.BULB_WITHOUT_AI);
 *     ((EntityAI) caster.getAI()).addSystemLog(LeekLog.STANDARD, Error.HELP_PAGE_LINK, new String[]{"summons"});
 *     return summonEntity(caster, target, template, null);
 *   }
 *   return state.useChip(caster, target, template);
 * }
 */
int lw_fight_use_chip(LwFight *self, struct LwEntity *caster,
                      struct LwCell *target, struct LwChip *template_) {
    struct LwAttack *attack = lw_chip_get_attack(template_);
    if (lw_attack_get_effect_parameters_by_type(attack, LW_EFFECT_TYPE_SUMMON) != NULL) {
        /* leek-log warnings -- stubbed (binding handles per-farmer logs). */
        (void)caster;
        return lw_fight_summon_entity(self, caster, target, template_, NULL);
    }
    return lw_state_use_chip(self->state, caster, target, template_);
}


/* ----------------------------------------------------------------- */
/* summonEntity                                                       */
/* ----------------------------------------------------------------- */

/* Java: public int summonEntity(Entity caster, Cell target, Chip template, FunctionLeekValue value) {
 *   return summonEntity(caster, target, template, value, null);
 * }
 */
int lw_fight_summon_entity(LwFight *self, struct LwEntity *caster,
                           struct LwCell *target, struct LwChip *template_,
                           struct LwFunctionLeekValue *value) {
    return lw_fight_summon_entity_named(self, caster, target, template_, value, NULL);
}

/* Java: public int summonEntity(Entity caster, Cell target, Chip template,
 *                               FunctionLeekValue value, String name) {
 *   int result = state.summonEntity(caster, target, template, name);
 *   // On assigne l'ia de l'invocation
 *   if (result > 0) {
 *     var summon = state.getLastEntity();
 *     summon.setFight(this);
 *     summon.setBirthTurn(getTurn());
 *     summon.setAI(new BulbAI(summon, (EntityAI) caster.getAI(), value));
 *   }
 *   return result;
 * }
 *
 * NOTE: BulbAI construction is owned by the binding (the Python AI
 * dispatcher); we set the AI handle to the FunctionLeekValue passed in. */
int lw_fight_summon_entity_named(LwFight *self, struct LwEntity *caster,
                                 struct LwCell *target, struct LwChip *template_,
                                 struct LwFunctionLeekValue *value,
                                 const char *name) {
    int result = lw_state_summon_entity_named(self->state, caster, target, template_, name);
    if (result > 0) {
        struct LwEntity *summon = lw_state_get_last_entity(self->state);
        lw_entity_set_fight(summon, self);
        lw_entity_set_birth_turn(summon, lw_fight_get_turn(self));
        /* setAI(new BulbAI(summon, caster.getAI(), value));
         * In C: store the FunctionLeekValue as the AI handle; the
         * dispatcher handles the BulbAI semantics. */
        (void)caster;
        lw_entity_set_ai(summon, value);
    }
    return result;
}


/* ----------------------------------------------------------------- */
/* generateCritical                                                   */
/* ----------------------------------------------------------------- */

/* Java: public boolean generateCritical(Entity caster) {
 *   return state.getRandom().getDouble() < ((double) caster.getAgility() / 1000);
 * }
 *
 * One RNG draw -- sacred order. */
int lw_fight_generate_critical(LwFight *self, struct LwEntity *caster) {
    uint64_t *rng = lw_state_get_random(self->state);
    double d = lw_rng_get_double(rng);
    return d < ((double)lw_entity_get_agility(caster) / 1000.0) ? 1 : 0;
}


/* ----------------------------------------------------------------- */
/* setStatisticsManager                                               */
/* ----------------------------------------------------------------- */

/* State setter (lw_state.c) */
void lw_state_set_statistics_manager(struct LwState *state, struct LwStatisticsManager *m);
/* Manager-side hook (lw_statistics.c) -- writes the back-pointer onto
 * the manager's user_data so callbacks can reach the fight. */
void lw_stats_set_generator_fight(struct LwStatisticsManager *m, LwFight *fight);

/* Java: public void setStatisticsManager(StatisticsManager statisticsManager) {
 *   this.state.setStatisticsManager(statisticsManager);
 *   statisticsManager.setGeneratorFight(this);
 * } */
void lw_fight_set_statistics_manager(LwFight *self, struct LwStatisticsManager *st) {
    if (self == NULL) return;
    lw_state_set_statistics_manager(self->state, st);
    lw_stats_set_generator_fight(st, self);
}


/* ----------------------------------------------------------------- */
/* Static type / context helpers                                      */
/* ----------------------------------------------------------------- */

/* The Fight.TYPE_SOLO_GARDEN, etc. constants are listed at the top of
 * Fight.java -- we reproduce the values inline for the helpers. */

#define LW_FIGHT_TYPE_SOLO_GARDEN        1
#define LW_FIGHT_TYPE_SOLO_TEST          2
#define LW_FIGHT_TYPE_NORMAL_WHAT        3
#define LW_FIGHT_TYPE_TEAM_GARDEN        4
#define LW_FIGHT_TYPE_SOLO_CHALLENGE     5
#define LW_FIGHT_TYPE_FARMER_GARDEN      6
#define LW_FIGHT_TYPE_SOLO_TOURNAMENT    7
#define LW_FIGHT_TYPE_TEAM_TEST          8
#define LW_FIGHT_TYPE_FARMER_TOURNAMENT  9
#define LW_FIGHT_TYPE_TEAM_TOURNAMENT   10
#define LW_FIGHT_TYPE_FARMER_CHALLENGE  11
#define LW_FIGHT_TYPE_FARMER_TEST       12
#define LW_FIGHT_FULL_TYPE_BATTLE_ROYALE 15

/* Java: public static int getFightContext(int type) {
 *   if (type == TYPE_SOLO_GARDEN || type == TYPE_TEAM_GARDEN || type == TYPE_FARMER_GARDEN) {
 *       return CONTEXT_GARDEN;
 *   } else if (type == TYPE_SOLO_TEST || type == TYPE_TEAM_TEST || type == TYPE_FARMER_TEST) {
 *       return CONTEXT_TEST;
 *   } else if (type == TYPE_TEAM_TOURNAMENT || type == TYPE_SOLO_TOURNAMENT || type == TYPE_FARMER_TOURNAMENT) {
 *       return CONTEXT_TOURNAMENT;
 *   } else if (type == FULL_TYPE_BATTLE_ROYALE) {
 *       return CONTEXT_BATTLE_ROYALE;
 *   }
 *   return CONTEXT_CHALLENGE;
 * }
 */
int lw_fight_get_fight_context(int type) {
    if (type == LW_FIGHT_TYPE_SOLO_GARDEN
     || type == LW_FIGHT_TYPE_TEAM_GARDEN
     || type == LW_FIGHT_TYPE_FARMER_GARDEN) {
        return LW_CONTEXT_GARDEN;
    } else if (type == LW_FIGHT_TYPE_SOLO_TEST
            || type == LW_FIGHT_TYPE_TEAM_TEST
            || type == LW_FIGHT_TYPE_FARMER_TEST) {
        return LW_CONTEXT_TEST;
    } else if (type == LW_FIGHT_TYPE_TEAM_TOURNAMENT
            || type == LW_FIGHT_TYPE_SOLO_TOURNAMENT
            || type == LW_FIGHT_TYPE_FARMER_TOURNAMENT) {
        return LW_CONTEXT_TOURNAMENT;
    } else if (type == LW_FIGHT_FULL_TYPE_BATTLE_ROYALE) {
        return LW_CONTEXT_BATTLE_ROYALE;
    }
    return LW_CONTEXT_CHALLENGE;
}

/* Java: public static int getFightType(int type) {
 *   if (type == TYPE_SOLO_GARDEN || type == TYPE_SOLO_CHALLENGE || type == TYPE_SOLO_TOURNAMENT || type == TYPE_SOLO_TEST) {
 *       return Fight.TYPE_SOLO;
 *   } else if (type == TYPE_FARMER_GARDEN || type == TYPE_FARMER_TOURNAMENT || type == TYPE_FARMER_CHALLENGE || type == TYPE_FARMER_TEST) {
 *       return Fight.TYPE_FARMER;
 *   } else if (type == FULL_TYPE_BATTLE_ROYALE) {
 *       return Fight.TYPE_BATTLE_ROYALE;
 *   }
 *   return Fight.TYPE_TEAM;
 * }
 */
int lw_fight_get_fight_type(int type) {
    if (type == LW_FIGHT_TYPE_SOLO_GARDEN
     || type == LW_FIGHT_TYPE_SOLO_CHALLENGE
     || type == LW_FIGHT_TYPE_SOLO_TOURNAMENT
     || type == LW_FIGHT_TYPE_SOLO_TEST) {
        return LW_FIGHT_TYPE_SOLO;
    } else if (type == LW_FIGHT_TYPE_FARMER_GARDEN
            || type == LW_FIGHT_TYPE_FARMER_TOURNAMENT
            || type == LW_FIGHT_TYPE_FARMER_CHALLENGE
            || type == LW_FIGHT_TYPE_FARMER_TEST) {
        return LW_FIGHT_TYPE_FARMER;
    } else if (type == LW_FIGHT_FULL_TYPE_BATTLE_ROYALE) {
        return LW_FIGHT_TYPE_BATTLE_ROYALE;
    }
    return LW_FIGHT_TYPE_TEAM;
}
