/*
 * lw_fight.h -- 1:1 port of fight/Fight.java
 *
 * The Java Fight class is the engine's per-fight orchestrator. It owns
 * the State (game tree), runs the turn loop, and dispatches each
 * entity's AI.
 *
 * The Java constants TYPE_, FULL_TYPE_, CONTEXT_, MAX_TURNS,
 * SUMMON_LIMIT, MAX_LOG_COUNT, FLAG_ are exported to lw_constants.h
 * with the LW_ prefix; this header doesn't redefine them.
 *
 * AI dispatch:
 *   In Java: Entity stores a leekscript AI handle; Fight.startTurn
 *   calls EntityAI.runTurn(turn).  In C we replace the leekscript
 *   handle with an opaque (callback, userdata) pair — the binding
 *   provides the implementation.  See lw_fight_ai_dispatch_t.
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/fight/Fight.java
 */
#ifndef LW_FIGHT_H
#define LW_FIGHT_H

#include <stdint.h>

#include "lw_constants.h"


/* ---- Fight-local constants -----------------------------------------
 *
 * Java's MAX_TURNS / SUMMON_LIMIT are re-exported from lw_constants.h
 * (LW_FIGHT_MAX_TURNS / LW_SUMMON_LIMIT). The remaining Fight.* constants
 * live here. */

/* Java: public final static int MAX_LOG_COUNT = 5000; */
#define LW_FIGHT_MAX_LOG_COUNT  5000

/* Java: public final static int FLAG_STATIC = 1;
 *       public final static int FLAG_PERFECT = 2; */
#define LW_FIGHT_FLAG_STATIC   1
#define LW_FIGHT_FLAG_PERFECT  2


/* Forward declarations. */
struct LwState;
struct LwGenerator;
struct LwEntity;
struct LwActions;
struct LwAttack;
struct LwEffect;
struct LwOrder;
struct LwMap;
struct LwTeam;
struct LwChip;
struct LwWeapon;
struct LwCell;
struct LwLeekLog;
struct LwFunctionLeekValue;
struct LwFightListener;


/* ---- AI dispatch hook ------------------------------------------------
 *
 * Java's `EntityAI.runTurn(turn)` hands control to leekscript bytecode.
 * The C engine doesn't understand leekscript — instead, the binding
 * registers a callback that runs the entity's AI for one turn and
 * returns the number of operations consumed.
 *
 * Returns:
 *   - operations consumed  (>= 0) on success
 *   - -1 on AI runtime error (caught by Fight.startTurn → ActionAIError)
 *
 * `ai_file` is the opaque AI handle stored on the entity (LwEntity.ai_file
 * or LwEntityInfo.ai_file).
 */
typedef int (*lw_fight_ai_dispatch_t)(struct LwFight *fight,
                                      struct LwEntity *entity,
                                      void *ai_file,
                                      int turn,
                                      void *userdata);


/* ---- LwFight --------------------------------------------------------- */

/* Java:
 *   private int mWinteam = -1;
 *   public Generator generator;
 *   private int mId;
 *   private int mBoss;
 *   private int mStartFarmer = -1;
 *   private int max_turns = MAX_TURNS;
 *   public long executionTime = 0;
 *   FightListener listener;
 *   private State state = new State();
 */
typedef struct LwFight {
    int                    m_winteam;       /* mWinteam */
    struct LwGenerator    *generator;
    int                    m_id;            /* mId */
    int                    m_boss;          /* mBoss */
    int                    m_start_farmer;  /* mStartFarmer */
    int                    max_turns;       /* private int max_turns = MAX_TURNS */
    int64_t                execution_time;  /* public long executionTime */
    struct LwFightListener *listener;
    struct LwState        *state;           /* owned externally; LwFight points to it */

    /* AI dispatcher (replaces Java reflection / leekscript) */
    lw_fight_ai_dispatch_t ai_dispatch;
    void                  *ai_dispatch_userdata;
} LwFight;


/* Java: public Fight(Generator generator) { this(generator, null); } */
void lw_fight_init(LwFight *self, struct LwGenerator *generator);

/* Java: public Fight(Generator generator, FightListener listener) */
void lw_fight_init_with_listener(LwFight *self, struct LwGenerator *generator,
                                 struct LwFightListener *listener);

/* Hook the AI dispatcher.  Called by the binding before startFight. */
void lw_fight_set_ai_dispatch(LwFight *self, lw_fight_ai_dispatch_t fn, void *userdata);

/* Bind state (Fight owns a State pointer; in C the State buffer is
 * allocated by the caller). */
void lw_fight_set_state(LwFight *self, struct LwState *state);


/* ---- Trivial accessors mirroring Java getters/setters ---------------- */

void lw_fight_add_flag(LwFight *self, int team, int flag);
int  lw_fight_get_winner(const LwFight *self);
void lw_fight_set_team_id(LwFight *self, int team, int id);
void lw_fight_set_start_farmer(LwFight *self, int start_farmer);
int  lw_fight_get_start_farmer(const LwFight *self);
int  lw_fight_get_team_id(const LwFight *self, int team);
int  lw_fight_get_id(const LwFight *self);
int  lw_fight_get_boss(const LwFight *self);
void lw_fight_set_id(LwFight *self, int id);
void lw_fight_set_boss(LwFight *self, int boss);
void lw_fight_set_max_turns(LwFight *self, int max_turns);
int  lw_fight_get_duration(const LwFight *self);
int  lw_fight_get_turn(const LwFight *self);
struct LwOrder* lw_fight_get_order(LwFight *self);
struct LwState* lw_fight_get_state(LwFight *self);
struct LwEntity* lw_fight_get_entity(LwFight *self, int64_t id);


/* ---- Core fight loop ------------------------------------------------- */

/* Java: public void startFight(boolean drawCheckLife) throws Exception
 *
 * Returns 0 on success, non-zero on FightException (NOT_ENOUGHT_PLAYERS
 * etc -- see initFight). The error message is recorded by the caller
 * via outcome.exception. */
int  lw_fight_start_fight(LwFight *self, int draw_check_life);

/* Java: public void startTurn() throws Exception */
int  lw_fight_start_turn(LwFight *self);

/* Java: public void initFight() throws FightException
 * Returns 0 on success, non-zero on a FightException-equivalent (with
 * mWinteam set when context == TOURNAMENT and one side is empty). */
int  lw_fight_init_fight(LwFight *self);

/* Java: public void finishFight() */
void lw_fight_finish_fight(LwFight *self);

/* Java: public void computeWinner(boolean drawCheckLife) */
void lw_fight_compute_winner(LwFight *self, int draw_check_life);


/* ---- Combat dispatch ------------------------------------------------- */

/* Java: public int useChip(Entity caster, Cell target, Chip template) */
int  lw_fight_use_chip(LwFight *self, struct LwEntity *caster,
                       struct LwCell *target, struct LwChip *template_);

/* Java: public int summonEntity(Entity caster, Cell target, Chip template,
 *                               FunctionLeekValue value) */
int  lw_fight_summon_entity(LwFight *self, struct LwEntity *caster,
                            struct LwCell *target, struct LwChip *template_,
                            struct LwFunctionLeekValue *value);

/* Java: public int summonEntity(Entity caster, Cell target, Chip template,
 *                               FunctionLeekValue value, String name) */
int  lw_fight_summon_entity_named(LwFight *self, struct LwEntity *caster,
                                  struct LwCell *target, struct LwChip *template_,
                                  struct LwFunctionLeekValue *value,
                                  const char *name);

/* Java: public boolean generateCritical(Entity caster) {
 *   return state.getRandom().getDouble() < ((double) caster.getAgility() / 1000);
 * }  -- one RNG draw, sacred order. */
int  lw_fight_generate_critical(LwFight *self, struct LwEntity *caster);


/* Java: public void setStatisticsManager(StatisticsManager statisticsManager) {
 *   this.state.setStatisticsManager(statisticsManager);
 *   statisticsManager.setGeneratorFight(this);
 * }
 *
 * NOTE: in C the StatisticsManager vtable carries no `fight` field of
 * its own (Java's setGeneratorFight stores `this` on the manager); the
 * binding wires that up via user_data. This function only forwards to
 * State and is provided for parity. */
void lw_fight_set_statistics_manager(LwFight *self, struct LwStatisticsManager *st);


/* ---- Static fight-type helpers -------------------------------------- */

/* Java: public static int getFightContext(int type) */
int  lw_fight_get_fight_context(int type);

/* Java: public static int getFightType(int type) */
int  lw_fight_get_fight_type(int type);


#endif /* LW_FIGHT_H */
