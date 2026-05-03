/*
 * lw_generator.c -- 1:1 port of Generator.java
 *
 * runScenario() is the engine entry point: it builds the Fight, wires
 * the State, creates entities + farmer logs, hands them to the AI
 * resolver (binding callback in C), runs the fight, and populates the
 * Outcome.
 *
 * The Java try/catch is replaced with a status return + outcome
 * exception field (see lw_outcome.h).
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/Generator.java
 */
#include "lw_generator.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "lw_constants.h"
#include "lw_outcome.h"
#include "lw_scenario.h"
#include "lw_actions.h"


/* ---- Forward declarations into the State / Entity / Farmer / AI / etc.
 *      modules. Mirror Java method signatures called from runScenario(). */

struct LwState;
struct LwEntity;
struct LwFightListener;
struct LwRegisterManager;
struct LwStatisticsManager;
struct LwLeekLog;
struct LwFarmerLog;

/* State setup helpers (lw_state.c) */
void              lw_state_init                (struct LwState *state);
void              lw_state_set_register_manager(struct LwState *state, void *rm);
void              lw_state_set_type           (struct LwState *state, int type);
void              lw_state_set_context        (struct LwState *state, int context);
void              lw_state_set_custom_map     (struct LwState *state, void *map);
void              lw_state_seed               (struct LwState *state, int seed);
void              lw_state_add_entity         (struct LwState *state, int team, struct LwEntity *entity);
struct LwActions* lw_state_get_actions        (struct LwState *state);
struct LwStatisticsManager* lw_state_get_statistics(struct LwState *state);
int               lw_state_get_duration       (const struct LwState *state);
int               lw_state_n_entities         (const struct LwState *state);
struct LwEntity*  lw_state_get_entity_at      (struct LwState *state, int idx);

/* Actions stream "dead" ObjectNode (Java: outcome.fight.dead = state.getDeadReport()).
 *
 * NOTE: in Java this is a JSON tree describing dead-entity reasons.
 * In C the LwActions struct does not carry the dead report -- the
 * Cython binding rebuilds it from LwState during serialisation. So we
 * skip the assignment in the C port (see runScenario below). */

/* Fight helpers -- declared in lw_fight.h but listed here for clarity:
 *   void lw_fight_set_statistics_manager(LwFight*, LwStatisticsManager*);
 *   void lw_fight_init_with_listener(LwFight*, LwGenerator*, LwFightListener*);
 *   void lw_fight_set_state(LwFight*, LwState*);
 *   void lw_fight_set_id/Boss/MaxTurns/AiDispatch(...);
 *   int  lw_fight_start_fight(LwFight*, int draw_check_life);
 *   void lw_fight_finish_fight(LwFight*);
 *   int  lw_fight_get_winner(const LwFight*);
 */

/* FarmerLog (lw_farmer_log.c) -- new FarmerLog(fight, farmer). */
struct LwFarmerLog* lw_farmer_log_new(LwFight *fight, int farmer);

/* LeekLog (lw_leek_log.c) -- new LeekLog(parent, entity). */
struct LwLeekLog*   lw_leek_log_new (struct LwFarmerLog *parent, struct LwEntity *entity);

/* Entity helpers (lw_entity.c) -- mirror Java Entity.* setters used by
 * Generator.runScenario for bookkeeping after createEntity. Signatures
 * mirror lw_entity.h (void* for opaque hooks, const for getters). */
void  lw_entity_set_fight    (struct LwEntity *entity, void *fight);
void  lw_entity_set_birth_turn(struct LwEntity *entity, int turn);
void  lw_entity_set_logs     (struct LwEntity *entity, void *logs);
void  lw_entity_set_ai_file  (struct LwEntity *entity, void *ai_file);
void* lw_entity_get_ai       (const struct LwEntity *entity);
int   lw_entity_is_summon    (const struct LwEntity *entity);
int   lw_entity_get_id       (const struct LwEntity *entity);
struct LwRegisters* lw_entity_get_registers(const struct LwEntity *entity);

/* Registers (lw_registers.c) */
int   lw_registers_is_modified(const struct LwRegisters *reg);
int   lw_registers_is_new     (const struct LwRegisters *reg);
const char* lw_registers_to_json_string(struct LwRegisters *reg);

/* RegisterManager (binding-supplied) */
void  lw_register_manager_save_registers(struct LwRegisterManager *rm,
                                         int entity_id,
                                         const char *json_string,
                                         int is_new);

/* AI resolver (binding-supplied callback). Returns the AI handle to
 * stash on the entity (or NULL).  Mirrors Java's EntityAI.resolve(this,
 * entityInfo, entity). */
typedef void* (*lw_ai_resolver_t)(LwGenerator *generator,
                                  const LwEntityInfo *entity_info,
                                  struct LwEntity *entity);

/* Storage for the resolver -- the binding registers it via the
 * Generator at startup (we keep it as a static pointer for simplicity;
 * a single resolver per process). */
static lw_ai_resolver_t s_ai_resolver = NULL;

void lw_generator_set_ai_resolver(lw_ai_resolver_t fn);
void lw_generator_set_ai_resolver(lw_ai_resolver_t fn) {
    s_ai_resolver = fn;
}

/* analyzeTime / compileTime accessor on the AI handle.  Stubbed; the
 * binding tracks these and feeds them via the Outcome directly. */
int64_t lw_ai_get_analyze_time(void *ai);
int64_t lw_ai_get_compile_time(void *ai);


/* ---- Logger stubs ------------------------------------------------ */

static void lw_log_i(const char *tag, const char *msg) { (void)tag; (void)msg; }
static void lw_log_e(const char *tag, const char *msg) { (void)tag; (void)msg; }
static void lw_log_s(const char *tag, const char *msg) { (void)tag; (void)msg; }


/* ----------------------------------------------------------------- */
/* Constructor                                                        */
/* ----------------------------------------------------------------- */

/* Java: public Generator() {
 *   new File("ai/").mkdir();
 *   LeekFunctions.setExtraFunctions(...);
 *   LeekConstants.setExtraConstants("com.leekwars.generator.FightConstants");
 *   loadWeapons();  loadChips();  loadSummons();  loadComponents();
 * }
 *
 * In C: just init the struct. Data-loading is manual (binding decides
 * which JSON files to read and calls lw_weapons_add_weapon etc.). */
void lw_generator_init(LwGenerator *self) {
    if (self == NULL) return;
    self->use_leekscript_cache = 1;
    self->error_handler = NULL;
    self->error_handler_userdata = NULL;
    self->ai_dispatch = NULL;
    self->ai_dispatch_userdata = NULL;
}


void lw_generator_set_ai_dispatch(LwGenerator *self,
                                  lw_fight_ai_dispatch_t fn,
                                  void *userdata) {
    if (self == NULL) return;
    self->ai_dispatch = fn;
    self->ai_dispatch_userdata = userdata;
}


/* Stubs -- the binding owns JSON parsing and calls the per-table
 * registration functions directly. */
void lw_generator_load_weapons   (LwGenerator *self) { (void)self; }
void lw_generator_load_chips     (LwGenerator *self) { (void)self; }
void lw_generator_load_summons   (LwGenerator *self) { (void)self; }
void lw_generator_load_components(LwGenerator *self) { (void)self; }


/* Java: public void setCache(boolean cache) { this.use_leekscript_cache = cache; } */
void lw_generator_set_cache(LwGenerator *self, int cache) {
    self->use_leekscript_cache = cache ? 1 : 0;
}

/* Java: public void exception(Throwable e) {
 *   if (errorManager != null) errorManager.exception(e, -1);
 * }
 */
void lw_generator_exception(LwGenerator *self, const char *message) {
    if (self == NULL) return;
    if (self->error_handler != NULL) {
        self->error_handler(self->error_handler_userdata, message);
    }
}

void lw_generator_exception_in_fight(LwGenerator *self,
                                     struct LwFight *fight,
                                     const char *message) {
    (void)fight;  /* fight id is recorded by the binding's handler */
    lw_generator_exception(self, message);
}


/* ----------------------------------------------------------------- */
/* runScenario                                                        */
/* ----------------------------------------------------------------- */

/* Java: public Outcome runScenario(Scenario scenario, FightListener listener,
 *                                  RegisterManager registerManager,
 *                                  StatisticsManager statisticsManager) {
 *
 *   Outcome outcome = new Outcome();
 *
 *   Fight fight = new Fight(this, listener);
 *   fight.getState().setRegisterManager(registerManager);
 *   fight.setStatisticsManager(statisticsManager);
 *   fight.setId(scenario.fightID);
 *   fight.setBoss(scenario.boss);
 *   fight.setMaxTurns(scenario.maxTurns);
 *   fight.getState().setType(scenario.type);
 *   fight.getState().setContext(scenario.context);
 *   fight.getState().setCustomMap(scenario.map);
 *   fight.getState().seed(scenario.seed);
 *
 *   // Create logs and compile AIs
 *   int t = 0;
 *   for (List<EntityInfo> team : scenario.entities) {
 *     for (EntityInfo entityInfo : team) {
 *       int aiOwner = entityInfo.aiOwner;
 *       if (entityInfo.type == Entity.TYPE_MOB) aiOwner = 0;
 *       if (entityInfo.type == Entity.TYPE_TURRET && entityInfo.team > 0) aiOwner = -entityInfo.team;
 *       if (!outcome.logs.containsKey(aiOwner)) {
 *         outcome.logs.put(aiOwner, new FarmerLog(fight, entityInfo.farmer));
 *       }
 *       var entity = entityInfo.createEntity(this, scenario, fight);
 *       fight.getState().addEntity(t, entity);
 *       entity.setFight(fight);
 *       entity.setBirthTurn(1);
 *
 *       entity.setLogs(new LeekLog(outcome.logs.get(aiOwner), entity));
 *       entity.setAIFile(EntityAI.resolve(this, entityInfo, entity));
 *     }
 *     t++;
 *   }
 *
 *   try {
 *     fight.startFight(scenario.drawCheckLife);
 *     fight.finishFight();
 *     outcome.fight = fight.getState().getActions();
 *     outcome.fight.dead = fight.getState().getDeadReport();
 *     outcome.winner = fight.getWinner();
 *     outcome.duration = fight.getState().getDuration();
 *     outcome.statistics = statisticsManager;
 *     for (var entity : fight.getState().getEntities().values()) {
 *       if (entity.getAI() != null) {
 *         outcome.analyzeTime += ((EntityAI) entity.getAI()).getAnalyzeTime();
 *         outcome.compilationTime += ((EntityAI) entity.getAI()).getCompileTime();
 *       }
 *     }
 *     outcome.executionTime = fight.executionTime;
 *
 *     for (var entity : fight.getState().getEntities().values()) {
 *       if (!entity.isSummon()
 *           && entity.getRegisters() != null
 *           && (entity.getRegisters().isModified() || entity.getRegisters().isNew())) {
 *         registerManager.saveRegisters(entity.getId(),
 *                                       entity.getRegisters().toJSONString(),
 *                                       entity.getRegisters().isNew());
 *       }
 *     }
 *     return outcome;
 *   } catch (Exception e) {
 *     outcome.exception = e;
 *     return outcome;
 *   }
 * }
 */
int lw_generator_run_scenario(LwGenerator *self,
                              LwScenario *scenario,
                              struct LwFightListener *listener,
                              struct LwRegisterManager *register_manager,
                              struct LwStatisticsManager *statistics_manager,
                              struct LwState *state,
                              LwFight *fight,
                              LwOutcome *outcome) {
    if (self == NULL || scenario == NULL || state == NULL
        || fight == NULL || outcome == NULL) {
        return -1;
    }

    /* Outcome outcome = new Outcome(); */
    lw_outcome_init(outcome);

    /* Fight fight = new Fight(this, listener); */
    lw_fight_init_with_listener(fight, self, listener);

    /* The C engine needs an explicit State binding (the Java Fight
     * allocates its own State internally). */
    lw_state_init(state);
    lw_fight_set_state(fight, state);

    /* Hook the AI dispatcher set on the generator -> the fight. */
    lw_fight_set_ai_dispatch(fight, self->ai_dispatch, self->ai_dispatch_userdata);

    /* fight.getState().setRegisterManager(registerManager); */
    lw_state_set_register_manager(state, (void*)register_manager);

    /* fight.setStatisticsManager(statisticsManager); */
    lw_fight_set_statistics_manager(fight, statistics_manager);

    /* fight.setId(scenario.fightID); */
    lw_fight_set_id(fight, scenario->fight_id);
    /* fight.setBoss(scenario.boss); */
    lw_fight_set_boss(fight, scenario->boss);
    /* fight.setMaxTurns(scenario.maxTurns); */
    lw_fight_set_max_turns(fight, scenario->max_turns);
    /* fight.getState().setType(scenario.type); */
    lw_state_set_type(state, scenario->type);
    /* fight.getState().setContext(scenario.context); */
    lw_state_set_context(state, scenario->context);
    /* fight.getState().setCustomMap(scenario.map); */
    /* fight.getState().setCustomMap(scenario.map);
     * Java passes the raw ObjectNode (JSON tree); the C state stores
     * an opaque pointer. We cast through (void*) — the Map.fromJson
     * step inside state.init() will read back our LwCustomMap struct. */
    lw_state_set_custom_map(state, (void*)&scenario->map);
    /* fight.getState().seed(scenario.seed); */
    lw_state_seed(state, (int)scenario->seed);

    /* Create logs and compile AIs.
     * int t = 0;
     * for (List<EntityInfo> team : scenario.entities) {
     *   for (EntityInfo entityInfo : team) { ... }
     *   t++;
     * }
     */
    for (int t = 0; t < scenario->n_entity_teams; t++) {
        for (int i = 0; i < scenario->n_entities[t]; i++) {
            const LwEntityInfo *entity_info = &scenario->entities[t][i];

            /* int aiOwner = entityInfo.aiOwner;
             * if (entityInfo.type == Entity.TYPE_MOB) aiOwner = 0;
             * // Turret logs are keyed by -team so all team members can see
             * // them, regardless of the AI owner's per-farmer logs_level.
             * if (entityInfo.type == Entity.TYPE_TURRET && entityInfo.team > 0)
             *     aiOwner = -entityInfo.team;
             */
            int ai_owner = entity_info->aiOwner;
            if (entity_info->type == LW_ENTITY_TYPE_MOB) {
                ai_owner = 0;
            }
            if (entity_info->type == LW_ENTITY_TYPE_TURRET && entity_info->team > 0) {
                ai_owner = -entity_info->team;
            }

            /* if (!outcome.logs.containsKey(aiOwner)) {
             *     outcome.logs.put(aiOwner, new FarmerLog(fight, entityInfo.farmer));
             * } */
            if (!lw_outcome_logs_contains_key(outcome, ai_owner)) {
                struct LwFarmerLog *flog = lw_farmer_log_new(fight, entity_info->farmer);
                lw_outcome_logs_put(outcome, ai_owner, flog);
            }

            /* var entity = entityInfo.createEntity(this, scenario, fight); */
            struct LwEntity *entity = lw_entity_info_create_entity(entity_info, self,
                                                                    scenario, fight);
            if (entity == NULL) continue;

            /* fight.getState().addEntity(t, entity); */
            lw_state_add_entity(state, t, entity);
            /* entity.setFight(fight); */
            lw_entity_set_fight(entity, fight);
            /* entity.setBirthTurn(1); */
            lw_entity_set_birth_turn(entity, 1);

            /* entity.setLogs(new LeekLog(outcome.logs.get(aiOwner), entity)); */
            struct LwFarmerLog *flog = lw_outcome_logs_get(outcome, ai_owner);
            lw_entity_set_logs(entity, lw_leek_log_new(flog, entity));

            /* entity.setAIFile(EntityAI.resolve(this, entityInfo, entity)); */
            void *ai_file = NULL;
            if (s_ai_resolver != NULL) {
                ai_file = s_ai_resolver(self, entity_info, entity);
            } else {
                /* Fall back to whatever the EntityInfo already carries
                 * (the binding may have pre-resolved the AI). */
                ai_file = entity_info->ai_file;
            }
            lw_entity_set_ai_file(entity, ai_file);
        }
    }

    /* try {
     *   fight.startFight(scenario.drawCheckLife);
     *   fight.finishFight();
     *   outcome.fight = fight.getState().getActions();
     *   outcome.fight.dead = fight.getState().getDeadReport();
     *   outcome.winner = fight.getWinner();
     *   outcome.duration = fight.getState().getDuration();
     *   outcome.statistics = statisticsManager;
     *   for (var entity : fight.getState().getEntities().values()) {
     *     if (entity.getAI() != null) {
     *       outcome.analyzeTime += ((EntityAI) entity.getAI()).getAnalyzeTime();
     *       outcome.compilationTime += ((EntityAI) entity.getAI()).getCompileTime();
     *     }
     *   }
     *   outcome.executionTime = fight.executionTime;
     *   ... save registers ...
     *   return outcome;
     * } catch (Exception e) {
     *   outcome.exception = e;
     *   return outcome;
     * }
     */
    lw_log_i("Generator", "Start fight...");
    int rc = lw_fight_start_fight(fight, scenario->draw_check_life);
    if (rc != 0) {
        /* The Java catch sets outcome.exception = e; we record a
         * placeholder message (the binding can override). */
        lw_outcome_set_exception(outcome, "fight failed");
        lw_log_e("Generator", "Error during fight generation!");
        return rc;
    }
    lw_fight_finish_fight(fight);

    /* outcome.fight = fight.getState().getActions(); */
    {
        struct LwActions *src = lw_state_get_actions(state);
        if (src != NULL) {
            /* Copy the action stream by value so the outcome owns a
             * stable snapshot.  lw_actions_copy mirrors the Java copy
             * constructor (Actions.actions list copy). */
            outcome->fight = *src;
        }
    }
    /* outcome.fight.dead = fight.getState().getDeadReport();
     * SKIPPED: the dead report is a JSON tree built by the Cython
     * binding from LwState; the C LwActions struct doesn't carry it. */

    /* outcome.winner = fight.getWinner(); */
    outcome->winner = lw_fight_get_winner(fight);
    /* outcome.duration = fight.getState().getDuration(); */
    outcome->duration = lw_state_get_duration(state);
    /* outcome.statistics = statisticsManager; */
    outcome->statistics = statistics_manager;

    /* for (var entity : fight.getState().getEntities().values()) {
     *   if (entity.getAI() != null) {
     *     outcome.analyzeTime    += ((EntityAI) entity.getAI()).getAnalyzeTime();
     *     outcome.compilationTime += ((EntityAI) entity.getAI()).getCompileTime();
     *   }
     * } */
    int n_entities = lw_state_n_entities(state);
    for (int i = 0; i < n_entities; i++) {
        struct LwEntity *entity = lw_state_get_entity_at(state, i);
        void *ai = lw_entity_get_ai(entity);
        if (ai != NULL) {
            outcome->analyze_time     += lw_ai_get_analyze_time(ai);
            outcome->compilation_time += lw_ai_get_compile_time(ai);
        }
    }
    /* outcome.executionTime = fight.executionTime; */
    outcome->execution_time = fight->execution_time;

    /* Save registers */
    for (int i = 0; i < n_entities; i++) {
        struct LwEntity *entity = lw_state_get_entity_at(state, i);
        if (lw_entity_is_summon(entity)) continue;

        struct LwRegisters *reg = lw_entity_get_registers(entity);
        if (reg != NULL && (lw_registers_is_modified(reg) || lw_registers_is_new(reg))) {
            lw_register_manager_save_registers(register_manager,
                                               lw_entity_get_id(entity),
                                               lw_registers_to_json_string(reg),
                                               lw_registers_is_new(reg));
        }
    }

    /* Log.i(TAG, "SHA-1: " + Util.sha1(outcome.toString())); -- skipped (binding) */
    lw_log_s("Generator", "Fight generated!");

    return 0;
}
