/*
 * lw_scenario.h -- 1:1 port of scenario/Scenario.java
 *
 * Java fields → C struct fields, snake_case for the C field name. The
 * Java class also exposes a fromFile() JSON loader that we omit -- the
 * binding builds LwScenario from Python dicts.
 *
 * TreeMap<Integer, FarmerInfo> / <Integer, TeamInfo>:
 *   keys are bounded farmer / team ids (always small positive ints in
 *   practice). We use linear-probe lookup tables sized to a generous
 *   upper bound; iteration in key-ascending order is preserved by
 *   walking the table by key.
 *
 * List<List<EntityInfo>>:
 *   "team t -> list of entities". Bounded fixed-size arrays (Java's
 *   ArrayList of ArrayList of EntityInfo).
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/scenario/Scenario.java
 */
#ifndef LW_SCENARIO_H
#define LW_SCENARIO_H

#include <stdint.h>

#include "lw_constants.h"
#include "lw_entity_info.h"


/* ---- bounds ----------------------------------------------------------
 * Battle royale tops out at ~16 teams * 8 entities; chest hunt 32 teams.
 * Farmer / team ids are bounded by the scenario builder. */
#define LW_SCENARIO_MAX_TEAMS    32
#define LW_SCENARIO_MAX_ENTITIES_PER_TEAM 16
#define LW_SCENARIO_MAX_FARMERS  64
#define LW_SCENARIO_MAX_TEAM_INFOS 32

/* Maximum custom-map width / height.  We keep the obstacle list opaque
 * (via the binding) -- the Java type here is ObjectNode (Jackson). */
#define LW_SCENARIO_MAP_OBSTACLES 256


/* Forward declarations. */
struct LwState;


/* ----- LwCustomMap ---------------------------------------------------
 *
 * Java: public ObjectNode map = null;  (raw JSON tree, applied in
 * State.setCustomMap by Map.fromJson()).
 *
 * For C parity we expose just the dimensions + obstacle list. The
 * Cython binding fills this; the engine reads back from it during
 * State.init(). */
typedef struct {
    int present;             /* 0 = no custom map (use default) */
    int id;                  /* map id; non-zero means "respect entity initial_cell" */
    int width;
    int height;
    int n_obstacles;
    /* Each obstacle: cell + type id (the Java map JSON uses an array of
     * [cell, type] pairs). */
    int obstacles_cell[LW_SCENARIO_MAP_OBSTACLES];
    int obstacles_type[LW_SCENARIO_MAP_OBSTACLES];
    /* Java: custom_map.team1 / .team2 = list of cell ids to place each
     * team's entities at. The Map.generateMap loop overrides the random
     * cell with these when present. */
    int team1[LW_SCENARIO_MAX_ENTITIES_PER_TEAM];
    int n_team1;
    int team2[LW_SCENARIO_MAX_ENTITIES_PER_TEAM];
    int n_team2;
} LwCustomMap;


/* ----- LwScenario ----------------------------------------------------
 *
 * Java:
 *   public int seed = 0;            // long upstream
 *   public int maxTurns = 64;
 *   public int type = 0;
 *   public int context = 0;
 *   public int fightID = 0;
 *   public int boss = 0;
 *   public Map<Integer, FarmerInfo> farmers = new HashMap<>();
 *   public Map<Integer, TeamInfo> teams = new HashMap<>();
 *   public List<List<EntityInfo>> entities = new ArrayList<>();
 *   public ObjectNode map = null;
 *   public boolean drawCheckLife = false;
 */
typedef struct {
    int64_t seed;                    /* Java: int seed (used as long via state.seed) */
    int     max_turns;
    int     type;                    /* LW_FIGHT_TYPE_* */
    int     context;                 /* LW_CONTEXT_* */
    int     fight_id;                /* Java: fightID */
    int     boss;

    /* Farmers (TreeMap<Integer, FarmerInfo>). NOTE: stored as a dense
     * table keyed by farmer.id; the "Leek Wars" farmer (id 0) is
     * synthesised by lw_scenario_get_farmer when missing. */
    LwFarmerInfo farmers[LW_SCENARIO_MAX_FARMERS];
    int          n_farmers;

    /* Teams (TreeMap<Integer, TeamInfo>). */
    LwTeamInfo teams[LW_SCENARIO_MAX_TEAM_INFOS];
    int        n_teams;

    /* List<List<EntityInfo>> entities -- per-team entity lists. The
     * outer list grows on addEntity(teamID, ...). */
    LwEntityInfo entities[LW_SCENARIO_MAX_TEAMS][LW_SCENARIO_MAX_ENTITIES_PER_TEAM];
    int          n_entities[LW_SCENARIO_MAX_TEAMS];
    int          n_entity_teams;     /* outer-list size */

    LwCustomMap  map;

    int          draw_check_life;    /* boolean */
} LwScenario;


/* Java: public Scenario() {
 *   this.seed = 1 + (int)((Math.random() * System.nanoTime()) % Integer.MAX_VALUE);
 * }
 *
 * NOTE: callers must set .seed explicitly before runScenario in C; we
 * skip the random default here (deterministic by default in C).
 */
void lw_scenario_init(LwScenario *self);

/* Java: public void addEntity(int teamID, EntityInfo entity) {
 *   if (entity == null || teamID < 0) return;
 *   while (entities.size() < teamID + 1) entities.add(new ArrayList<>());
 *   entities.get(teamID).add(entity);
 * }
 *
 * In C, `entity` is a value passed by pointer that we copy into the
 * scenario's storage (Java uses references but the EntityInfo itself
 * is treated as immutable post-construction). */
void lw_scenario_add_entity(LwScenario *self, int team_id, const LwEntityInfo *entity);

/* Java: scenario.farmers.put(farmer.id, farmer); */
void lw_scenario_add_farmer(LwScenario *self, const LwFarmerInfo *farmer);

/* Java: scenario.teams.put(team.id, team); */
void lw_scenario_add_team(LwScenario *self, const LwTeamInfo *team);


/* Java: public FarmerInfo getFarmer(int farmer) {
 *   if (farmer == 0) return leekwarsFarmer;
 *   else return this.farmers.get(farmer);
 * }
 *
 * The static leekwarsFarmer has id=0, name="Leek Wars", country="fr". */
const LwFarmerInfo* lw_scenario_get_farmer(const LwScenario *self, int farmer);

/* Java: scenario.teams.get(team)  (no public accessor in Java; created
 * here for parity with the engine's only call site). */
const LwTeamInfo* lw_scenario_get_team(const LwScenario *self, int team);


/* Java: public void setEntityAI(int team, int leek_id, String aiName,
 *                                int aiFolder, String aiPath, int aiOwner,
 *                                int aiVersion, boolean aiStrict) {
 *   for (EntityInfo entity : entities.get(team)) {
 *     if (entity.id == leek_id) { ... entity.ai = aiName; ... }
 *   }
 * }
 */
void lw_scenario_set_entity_ai(LwScenario *self, int team, int leek_id,
                               const char *ai_name, int ai_folder,
                               const char *ai_path, int ai_owner,
                               int ai_version, int ai_strict);

/* Java: public void setDrawCheckLife(boolean drawCheckLife) */
static inline void lw_scenario_set_draw_check_life(LwScenario *self, int v) {
    self->draw_check_life = v ? 1 : 0;
}


#endif /* LW_SCENARIO_H */
