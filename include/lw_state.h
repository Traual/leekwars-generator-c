/*
 * lw_state.h -- 1:1 port of state/State.java
 *
 * LwState is the root struct of the engine: it owns the map, the
 * entities, the team list, the rolling order, the action stream, the
 * RNG state, the statistics manager, etc.  Almost every other file
 * takes an `LwState *state` as the first arg, mirroring how Java's
 * State is the root passed through every method.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/State.java
 */
#ifndef LW_STATE_H
#define LW_STATE_H

#include <stdint.h>

#include "lw_constants.h"
#include "lw_action_stream.h"
#include "lw_actions.h"
#include "lw_order.h"
#include "lw_start_order.h"
#include "lw_statistics.h"


/* ---- Forward declarations -- ported in parallel files ----------- */
struct LwMap;
struct LwEntity;
struct LwTeam;
struct LwAttack;
struct LwEffect;
struct LwChip;
struct LwWeapon;
struct LwItem;
struct LwCell;


/* ---- Capacities --------------------------------------------------- *
 *
 * The Java code uses ArrayList/HashMap with no fixed bound. In C we
 * pre-allocate. Bounds are conservative upper limits for any fight
 * type (battle royale up to 8v0, plus 8 summons each).
 */
#define LW_STATE_MAX_TEAMS       16
#define LW_STATE_MAX_ENTITIES    64
#define LW_STATE_MAX_COOLDOWNS   256


/* ---- Per-entity cooldown record (entity owns its own cooldowns
 *      table; State only stores team-level via LwTeam, but for the
 *      simple state operations we need a generic id->turns lookup
 *      pattern here). Keep the actual storage on LwEntity / LwTeam. */


/*
 * Java fields, in source order:
 *
 *   private RandomGenerator randomGenerator;       (LCG state)
 *   private final List<Team> teams;
 *   private final List<Entity> initialOrder;
 *   private int mNextEntityId = 0;
 *   private int mWinteam = -1;
 *   private final java.util.Map<Integer, Entity> mEntities;
 *   private int mId;
 *   public int mState = STATE_INIT;
 *   private Order order;
 *   private final int fullType;
 *   private int mStartFarmer = -1;
 *   private int lastTurn = 0;
 *   private int colossusMultiplier = 0;
 *   private Date date;
 *   private Map map;
 *   private final Actions actions;
 *   private String mLeekDatas = "";
 *   private int context;
 *   private int type;
 *   public ObjectNode custom_map = null;
 *   public StatisticsManager statistics;
 *   private RegisterManager registerManager;
 *   public long executionTime = 0;
 *   private int seed = 0;
 */
typedef struct LwState {

    /* RNG (private RandomGenerator with deterministic LCG). */
    uint64_t              rng_n;

    /* Teams. */
    struct LwTeam        *teams[LW_STATE_MAX_TEAMS];
    int                   n_teams;

    /* Initial play order (snapshot of the StartOrder.compute() result). */
    struct LwEntity      *initial_order[LW_STATE_MAX_ENTITIES];
    int                   n_initial_order;

    int                   m_next_entity_id;
    int                   m_winteam;

    /* Entities, indexed by entity-array-index (insertion order).
     * Java uses HashMap<Integer, Entity>; we use a flat array with a
     * separate id->index lookup table so iteration order matches Java's
     * HashMap-iteration order under our deterministic insertion. */
    struct LwEntity      *m_entities[LW_STATE_MAX_ENTITIES];
    int                   n_entities;

    int                   m_id;
    int                   m_state;          /* LW_FIGHT_STATE_* */

    LwOrder               order;

    int                   full_type;
    int                   m_start_farmer;
    int                   last_turn;
    int                   colossus_multiplier;

    /* (Java: Date date)  Wall-clock timestamp at construction.  Not
     * relevant to determinism; stored as epoch-seconds for parity with
     * any consumer that wants it. */
    int64_t               date;

    struct LwMap         *map;

    LwActions             actions;

    /* (Java: String mLeekDatas)  Cached JSON-string of [level, skin, hat]
     * per entity, built in init().  Stored verbatim. */
    char                  m_leek_datas[1024];

    int                   context;          /* LW_CONTEXT_* */
    int                   type;             /* LW_FIGHT_TYPE_* */

    /* (Java: ObjectNode custom_map)  Optional custom-map JSON; opaque
     * pointer here (Cython binding fills it in). */
    void                 *custom_map;

    LwStatisticsManager  *statistics;

    /* (Java: RegisterManager)  Opaque pointer; AI register persistence
     * is owned by the binding. */
    void                 *register_manager;

    int64_t               execution_time;
    int                   seed;

    /* drawCheckLife - passed as an arg to computeWinner in Java; cached
     * here so callers can flip the policy globally. */
    int                   draw_check_life;

    /* listener (Fight in Java) -- opaque pointer to the Fight runner,
     * used only by Entity callbacks; not consumed inside State. */
    void                 *listener;
} LwState;


/* ---- Construction / init ---------------------------------------- */

/* public State() -- default constructor.  Sets up empty teams/entities,
 * action stream, RNG seed = 0, type = SOLO, context = GARDEN, full_type
 * = TYPE_SOLO_GARDEN. */
void lw_state_init(LwState *self);


/* public State(State state) -- deep-copy constructor.  Walks the source
 * state and rebuilds entities, effects, teams, order, map.
 *
 * NOTE: In C this requires a per-entity clone hook from lw_entity.c.
 * Stub for now -- raises a NULL deref if called before that lands. */
void lw_state_clone(LwState *self, const LwState *src);


/* ---- Field accessors -------------------------------------------- */

/* public void seed(int seed) */
void lw_state_seed(LwState *self, int seed);

/* public RandomGenerator getRandom() -- returns &rng_n */
uint64_t* lw_state_get_random(LwState *self);

/* public Map getMap() */
struct LwMap* lw_state_get_map(LwState *self);

/* public Actions getActions() */
LwActions* lw_state_get_actions(LwState *self);

/* public Order getOrder() */
LwOrder* lw_state_get_order(LwState *self);

/* public int getState()  /  setState */
int  lw_state_get_state(const LwState *self);
void lw_state_set_state(LwState *self, int state);

/* public int getTurn() -- == order.getTurn() */
int  lw_state_get_turn(const LwState *self);

/* public int getMaxTurns() -- alias of LW_FIGHT_MAX_TURNS */
int  lw_state_get_max_turns(const LwState *self);

/* public int getType() */
int  lw_state_get_type(const LwState *self);

/* public int getContext() */
int  lw_state_get_context(const LwState *self);

/* public int getFullType() */
int  lw_state_get_full_type(const LwState *self);

/* public void setType(int type) */
void lw_state_set_type(LwState *self, int type);

/* public void setContext(int context) */
void lw_state_set_context(LwState *self, int context);

/* public void setId(int f)  /  getId */
void lw_state_set_id(LwState *self, int id);
int  lw_state_get_id(const LwState *self);

/* public void setStatisticsManager(StatisticsManager statisticsManager) */
void lw_state_set_statistics_manager(LwState *self, LwStatisticsManager *m);

/* public void setRegisterManager(RegisterManager registerManager)  /  get */
void  lw_state_set_register_manager(LwState *self, void *rm);
void* lw_state_get_register_manager(LwState *self);

/* public int getWinner() */
int  lw_state_get_winner(const LwState *self);

/* public void setStartFarmer(int) / getStartFarmer */
void lw_state_set_start_farmer(LwState *self, int v);
int  lw_state_get_start_farmer(const LwState *self);

/* public int getDuration() -- order.getTurn() */
int  lw_state_get_duration(const LwState *self);

/* public long getSeed() */
int64_t lw_state_get_seed(const LwState *self);

/* public String getLeekDatas() */
const char* lw_state_get_leek_datas(const LwState *self);

/* public void setCustomMap(ObjectNode map) */
void lw_state_set_custom_map(LwState *self, void *map);

/* public Entity getLastEntity() */
struct LwEntity* lw_state_get_last_entity(LwState *self);


/* ---- Entity / Team management ----------------------------------- */

/* public int getNextEntityId() */
int  lw_state_get_next_entity_id(LwState *self);

/* public void addEntity(int t, Entity entity) */
void lw_state_add_entity(LwState *self, int t, struct LwEntity *entity);

/* public Entity getEntity(int id) -- linear lookup over m_entities[]. */
struct LwEntity* lw_state_get_entity(LwState *self, int id);

/* public java.util.Map<Integer, Entity> getEntities() -- returns
 * caller-friendly array view. */
struct LwEntity** lw_state_get_entities(LwState *self, int *out_n);

/* public List<Entity> getEnemiesEntities(int team) */
int lw_state_get_enemies_entities(LwState *self, int team,
                                  struct LwEntity **out_buf, int out_cap);

/* public List<Entity> getEnemiesEntities(int team, boolean get_deads) */
int lw_state_get_enemies_entities_ex(LwState *self, int team, int get_deads,
                                     struct LwEntity **out_buf, int out_cap);

/* public List<Entity> getTeamEntities(int team) */
int lw_state_get_team_entities(LwState *self, int team,
                               struct LwEntity **out_buf, int out_cap);

/* public List<Entity> getTeamEntities(int team, boolean dead) */
int lw_state_get_team_entities_ex(LwState *self, int team, int dead,
                                  struct LwEntity **out_buf, int out_cap);

/* public List<Entity> getTeamLeeks(int team) */
int lw_state_get_team_leeks(LwState *self, int team,
                            struct LwEntity **out_buf, int out_cap);

/* public List<Entity> getAllEntities(boolean get_deads) */
int lw_state_get_all_entities(LwState *self, int get_deads,
                              struct LwEntity **out_buf, int out_cap);

/* public List<Team> getTeams() */
struct LwTeam** lw_state_get_teams(LwState *self, int *out_n);

/* public void setTeamID(int team, int id) */
void lw_state_set_team_id(LwState *self, int team, int id);

/* public int getTeamID(int team) */
int  lw_state_get_team_id(LwState *self, int team);

/* public void addFlag(int team, int flag) -- delegates to LwTeam. */
void lw_state_add_flag(LwState *self, int team, int flag);


/* ---- Action stream shortcuts ------------------------------------ */

/* public void log(Action log) -- shortcut for actions.log(...) */
void lw_state_log(LwState *self, const LwActionLog *entry);


/* ---- Fight lifecycle -------------------------------------------- */

/* public void init() -- THE BIG ONE */
void lw_state_init_fight(LwState *self);

/* public void startTurn() */
void lw_state_start_turn(LwState *self);

/* public void endTurn() */
void lw_state_end_turn(LwState *self);

/* public boolean isFinished() */
int  lw_state_is_finished(LwState *self);

/* public void computeWinner(boolean drawCheckLife) */
void lw_state_compute_winner(LwState *self, int draw_check_life);


/* ---- Combat ----------------------------------------------------- */

/* public boolean setWeapon(Entity entity, Weapon weapon) */
int  lw_state_set_weapon(LwState *self, struct LwEntity *entity, struct LwWeapon *weapon);

/* public int useWeapon(Entity launcher, Cell target) */
int  lw_state_use_weapon(LwState *self, struct LwEntity *launcher, struct LwCell *target);

/* public int useChip(Entity caster, Cell target, Chip template) */
int  lw_state_use_chip(LwState *self, struct LwEntity *caster, struct LwCell *target, struct LwChip *template_);

/* public boolean generateCritical(Entity caster) -- 1 RNG draw */
int  lw_state_generate_critical(LwState *self, struct LwEntity *caster);


/* ---- Movement --------------------------------------------------- */

/* public int moveEntity(Entity entity, List<Cell> path) */
int  lw_state_move_entity_path(LwState *self, struct LwEntity *entity,
                                struct LwCell *const *path, int path_len);

/* public void moveEntity(Entity entity, Cell cell) */
void lw_state_move_entity_cell(LwState *self, struct LwEntity *entity, struct LwCell *cell);

/* public void teleportEntity(Entity entity, Cell cell, Entity caster, int itemId) */
void lw_state_teleport_entity(LwState *self, struct LwEntity *entity, struct LwCell *cell,
                              struct LwEntity *caster, int item_id);

/* public void slideEntity(Entity entity, Cell cell, Entity caster) */
void lw_state_slide_entity(LwState *self, struct LwEntity *entity, struct LwCell *cell,
                           struct LwEntity *caster);

/* public void invertEntities(Entity caster, Entity target) */
void lw_state_invert_entities(LwState *self, struct LwEntity *caster, struct LwEntity *target);

/* public long moveToward(Entity entity, long leek_id, long pm_to_use) */
int64_t lw_state_move_toward(LwState *self, struct LwEntity *entity, int64_t leek_id, int64_t pm_to_use);

/* public long moveTowardCell(Entity entity, long cell_id, long pm_to_use) */
int64_t lw_state_move_toward_cell(LwState *self, struct LwEntity *entity, int64_t cell_id, int64_t pm_to_use);


/* ---- Summons / Resurrect ---------------------------------------- */

/* public int summonEntity(Entity caster, Cell target, Chip template) */
int lw_state_summon_entity(LwState *self, struct LwEntity *caster, struct LwCell *target,
                            struct LwChip *template_);

/* public int summonEntity(Entity caster, Cell target, Chip template, String name) */
int lw_state_summon_entity_named(LwState *self, struct LwEntity *caster, struct LwCell *target,
                                 struct LwChip *template_, const char *name);

/* public Bulb createSummon(Entity owner, int type, Cell target, int level, boolean critical, String name) */
struct LwEntity* lw_state_create_summon(LwState *self, struct LwEntity *owner, int type,
                                        struct LwCell *target, int level, int critical,
                                        const char *name);

/* public void removeInvocation(Entity invoc, boolean force) */
void lw_state_remove_invocation(LwState *self, struct LwEntity *invoc, int force);

/* public int resurrectEntity(Entity caster, Cell target, Chip template, Entity target_entity, boolean fullLife) */
int lw_state_resurrect_entity(LwState *self, struct LwEntity *caster, struct LwCell *target,
                              struct LwChip *template_, struct LwEntity *target_entity, int full_life);

/* public void resurrect(Entity owner, Entity entity, Cell cell, boolean critical, boolean fullLife) */
void lw_state_resurrect(LwState *self, struct LwEntity *owner, struct LwEntity *entity,
                        struct LwCell *cell, int critical, int full_life);


/* ---- Cooldowns -------------------------------------------------- */

/* public void addCooldown(Entity entity, Chip chip) */
void lw_state_add_cooldown_default(LwState *self, struct LwEntity *entity, struct LwChip *chip);

/* public void addCooldown(Entity entity, Chip chip, int cooldown) */
void lw_state_add_cooldown(LwState *self, struct LwEntity *entity, struct LwChip *chip, int cooldown);

/* public boolean hasCooldown(Entity entity, Chip chip) */
int  lw_state_has_cooldown(LwState *self, struct LwEntity *entity, struct LwChip *chip);

/* public int getCooldown(Entity entity, Chip chip) */
int  lw_state_get_cooldown(LwState *self, struct LwEntity *entity, struct LwChip *chip);


/* ---- Death / kill hooks ----------------------------------------- */

/* public void onPlayerDie(Entity entity, Entity killer, Item item) */
void lw_state_on_player_die(LwState *self, struct LwEntity *entity,
                            struct LwEntity *killer, struct LwItem *item);


/* ---- Battle Royale / Colossus pulses ---------------------------- */

/* public void giveBRPower() */
void lw_state_give_br_power(LwState *self);


/* ---- Static helpers --------------------------------------------- *
 *
 * These mirror the Java static methods on State and don't need a
 * `self`.  Kept inline for brevity. */

/* public static int getFightContext(int type) */
int lw_state_get_fight_context_static(int type);

/* public static int getFightType(int type) */
int lw_state_get_fight_type_static(int type);

/* public static boolean isTeamAIFight(int type) */
int lw_state_is_team_ai_fight(int type);

/* public static boolean isTestFight(int type) */
int lw_state_is_test_fight(int type);

/* public static boolean isChallenge(int type) */
int lw_state_is_challenge(int type);


/* ---- Progression ------------------------------------------------- */

/* public double getProgress() */
double lw_state_get_progress(LwState *self);


#endif /* LW_STATE_H */
