/*
 * lw_map.h -- 1:1 port of maps/Map.java
 *
 * Java:   public class Map
 *           private final int id;
 *           private final List<Cell> cells;
 *           private final int height;
 *           private final int width;
 *           private final int nb_cells;
 *           private int type;
 *           private final Cell[][] coord;
 *           private Cell[] mObstacles = null;     -- not ported (lazy cache, never mutated externally)
 *           private int min_x = -1; ... max_y;
 *           private ObjectNode custom_map;        -- consumed inline in generateMap; not stored after
 *           private HashMap<Entity, Cell> cellByEntity;   -- mirrored on LwEntity.cell_id (entity already knows its cell)
 *           private HashMap<Cell, Entity> entityByCell;   -- dense int[] entity_at_cell[N] indexed by cell id
 *           private ArrayNode pattern;
 *           private State state;                  -- not stored (state is passed in to each method instead)
 *
 * Custom-map JSON ingestion is left out -- the v2 engine accepts pre-cooked
 * obstacle/team-position arrays via the lw_map_generate_map signature.
 *
 * RNG draws are sacred: getRandomCell, getRandomCellNearCenter,
 * getRandomCellAwayFromCenter, getRandomCellAtDistance, getCellEqualDistance,
 * generateMap MUST consume the same number of random doubles in the same
 * order as the Java source. The byte-for-byte action-stream parity test
 * depends on it.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Map.java
 */
#ifndef LW_MAP_H
#define LW_MAP_H

#include "lw_cell.h"
#include "lw_constants.h"

#include <stdint.h>


/* Java: public final static byte NORTH = 0;// NE
 *       public final static byte EAST  = 1;// SE
 *       public final static byte SOUTH = 2;// SO
 *       public final static byte WEST  = 3;// NO
 *
 * Already declared in lw_constants.h as LW_DIR_NORTH/EAST/SOUTH/WEST
 * (verified to match Map.NORTH/EAST/SOUTH/WEST exactly). */


/* Hard size caps -- official maps are at most 17x17 hex variants (~613
 * cells). We size to 700 with a 64x64 coord LUT to cover the diamond
 * bounding box. Matches the v1 engine's limits. */
#define LW_MAP_MAX_CELLS    700
#define LW_MAP_COORD_DIM    64


/* Forward declarations -- these structs live in (or will live in)
 * their own headers. We only need pointer types here. */
struct LwState;
struct LwEntity;
struct LwLeek;
struct LwAttack;
struct LwTeam;


/* Java: public class Map */
typedef struct LwMap {

    /* private final int id; */
    int      id;

    /* private final int width / height; */
    int      width;
    int      height;

    /* private final int nb_cells; */
    int      nb_cells;

    /* Largest cell id present in cells[]. With nb_cells = N the ids run
     * 0..N-1 contiguously, so this equals nb_cells-1 -- exposed for
     * callers that need a bound on cell-id loops. */
    int      max_cell_id;

    /* private int type; */
    int      type;

    /* private int min_x = -1; max_x; min_y; max_y;
     * Initialized to -1 in the Java ctor, then updated as cells are
     * added. We keep the same semantics. */
    int      min_x, max_x, min_y, max_y;

    /* private final List<Cell> cells;
     * Stored inline -- LwCell is plain data and the count is bounded. */
    LwCell   cells[LW_MAP_MAX_CELLS];

    /* private final Cell[][] coord;
     * coord[gx][gy] = cell id at (gx + min_x, gy + min_y), or -1 if no
     * cell at that spot.  Java uses Cell* references; we use ids
     * (-1 == null) so the struct stays POD-clonable. */
    int      coord[LW_MAP_COORD_DIM][LW_MAP_COORD_DIM];

    /* private HashMap<Cell, Entity> entityByCell;
     * NOTE: dense int[] indexed by cell id; holds entity-array-index
     * (state->entities[idx]) or -1 for empty.  Java uses HashMap; this
     * is functionally identical with O(1) lookup and is POD-clonable. */
    int      entity_at_cell[LW_MAP_MAX_CELLS];

    /* private State state;
     * Forward-declared opaque pointer. The Java field is set in the
     * ctor and read by various methods; we keep it for parity but every
     * mutating method also takes `state` explicitly. */
    struct LwState *state;

    /* Number of connected components computed by computeComposantes().
     * Java doesn't expose this directly (composante ids live on each
     * Cell), but it's a useful debugging field for the C side. */
    int      composantes;

    /* private ArrayNode pattern;
     * Source of map texture pattern in the Java engine; opaque to the
     * fight logic. Stored as a single int (pattern id) here; set by
     * generateMap from the custom-map data when present, else 0. */
    int      pattern;

} LwMap;


/* ---- Constructors ----------------------------------------------- */

/* public Map(int width, int height) {
 *     this(width, height, 0);
 * }
 */
void lw_map_init(LwMap *self, int width, int height);

/* public Map(int width, int height, int id) */
void lw_map_init_with_id(LwMap *self, int width, int height, int id);


/* ---- Static factory --------------------------------------------- */

/* public static Map generateMap(State state, int context, int width,
 *                               int height, int obstacles_count,
 *                               List<Team> teams, ObjectNode custom_map)
 *
 * Populates `*out_map`. RNG draws happen in the same order as the Java
 * source (critical for parity).
 *
 * The JSON-tree custom_map argument is unraveled into the C-side
 * arrays the caller provides:
 *   - custom_obstacles / n_custom_obstacles : (cell_id, kind) pairs.
 *     kind > 0: obstacle id (looked up in lw_obstacle_info_get).
 *     kind == 0: boolean "true" obstacle (size 1 / id 1, matching
 *                Java's c.getValue().isBoolean() branch).
 *   - team1_cells / n_team1_cells, team2_cells / n_team2_cells : optional
 *     custom cells for teams 0 and 1; pass NULL/0 to skip.
 *   - custom_map_id, custom_pattern, custom_type : the corresponding
 *     fields of the JSON ObjectNode; custom_type < 0 means "absent"
 *     (matches Java's custom_map.has("type") false branch).
 *
 * If custom_obstacles is NULL the function uses the random map path
 * (matches Java's `custom_map == null` branch).
 *
 * Teams: opaque pointer + count. The function calls back into
 * lw_team_get_entity_count / lw_team_get_entity to enumerate live
 * leeks (declared at the bottom of this file).
 */
typedef struct {
    int cell_id;
    int kind;          /* >0: obstacle id, 0: boolean true (size 1, id 1) */
} LwCustomObstacle;

/* Wrapper signature (used by lw_state.c) -- defined in lw_glue.c. */
struct LwMap* lw_map_generate_map(struct LwState *state, int context,
                                   int width, int height, int obstacles_count,
                                   struct LwTeam **teams, int n_teams,
                                   void *custom_map);

/* Renamed full impl with split custom-map fields. */
void lw_map_generate_map_impl(LwMap *out_map,
                         struct LwState *state,
                         int context,
                         int width, int height,
                         int obstacles_count,
                         struct LwTeam **teams, int n_teams,
                         /* custom_map fields, or NULL/-1 sentinels for
                          * "no custom map" */
                         int custom_map_id,
                         int custom_pattern,
                         int custom_type,
                         const LwCustomObstacle *custom_obstacles,
                         int n_custom_obstacles,
                         const int *team1_cells, int n_team1_cells,
                         const int *team2_cells, int n_team2_cells);


/* ---- Entity book-keeping --------------------------------------- */

/* public void setEntity(Entity entity, Cell cell)
 *
 * `entity_idx` is the index into state->entities[] (Java holds the
 * Entity reference itself; we hold its array slot). */
void lw_map_set_entity(LwMap *self, struct LwEntity *entity, LwCell *cell);

/* public void moveEntity(Entity entity, Cell cell) */
int lw_map_move_entity(LwMap *self, struct LwEntity *entity, LwCell *cell);

/* public void removeEntity(Entity entity) */
void lw_map_remove_entity(LwMap *self, int entity_idx);

/* public void invertEntities(Entity entity1, Entity entity2) */
void lw_map_invert_entities(LwMap *self, int entity_idx1, int entity_idx2);

/* public Entity getEntity(Cell cell)
 *
 * Returns entity-array-index, or -1 if no entity occupies the cell. */
int lw_map_get_entity(const LwMap *self, const LwCell *cell);


/* ---- Trivial getters -------------------------------------------- */

/* public int getNbCell() { return nb_cells; } */
static inline int lw_map_get_nb_cell(const LwMap *self) { return self->nb_cells; }

/* public int getWidth()  { return width; } */
static inline int lw_map_get_width (const LwMap *self) { return self->width;  }

/* public int getHeight() { return height; } */
static inline int lw_map_get_height(const LwMap *self) { return self->height; }

/* public int getType()   { return type; } */
static inline int lw_map_get_type  (const LwMap *self) { return self->type; }

/* public void setType(int type) { this.type = type; } */
static inline void lw_map_set_type (LwMap *self, int type) { self->type = type; }

/* public int getId()     { return id; } */
static inline int lw_map_get_id    (const LwMap *self) { return self->id; }

/* public boolean isCustom() { return custom_map != null; }
 * NOTE: We don't store custom_map; pattern != 0 is a proxy used
 * elsewhere. This getter is currently unused by the engine; kept here
 * for parity. */
static inline int lw_map_is_custom(const LwMap *self) { return self->pattern != 0 ? 1 : 0; }


/* ---- Cell lookup ------------------------------------------------ */

/* public Cell getCell(int id) */
LwCell* lw_map_get_cell(LwMap *self, int id);
const LwCell* lw_map_get_cell_const(const LwMap *self, int id);

/* public Cell getCell(int x, int y) */
LwCell* lw_map_get_cell_xy(LwMap *self, int x, int y);
const LwCell* lw_map_get_cell_xy_const(const LwMap *self, int x, int y);

/* public Cell getCellByDir(Cell c, byte dir) */
LwCell* lw_map_get_cell_by_dir(LwMap *self, const LwCell *c, int dir);

/* public Cell getNextCell(Cell cell, int dx, int dy) */
LwCell* lw_map_get_next_cell(LwMap *self, const LwCell *cell, int dx, int dy);

/* public Cell[] getCellsAround(Cell c) -- writes 4 cell ids (or -1)
 * into out[0..3] in order: SOUTH, WEST, NORTH, EAST. */
void lw_map_get_cells_around(LwMap *self, const LwCell *c, LwCell *out[4]);


/* ---- Random / distance pickers --------------------------------- */

/* public Cell getRandomCell(State state) */
LwCell* lw_map_get_random_cell(LwMap *self, struct LwState *state);

/* public Cell getRandomCell(State state, int part)
 *
 * NOTE: makes 2 RNG draws per attempt (y first, then x). The byte-for-byte
 * parity test depends on this exact ordering. */
LwCell* lw_map_get_random_cell_part(LwMap *self, struct LwState *state, int part);

/* public Cell getRandomCellNearCenter(State state, int maxDistance) */
LwCell* lw_map_get_random_cell_near_center(LwMap *self, struct LwState *state, int max_distance);

/* public Cell getRandomCellAwayFromCenter(State state, int minDistance) */
LwCell* lw_map_get_random_cell_away_from_center(LwMap *self, struct LwState *state, int min_distance);

/* public Cell getRandomCellAtDistance(Cell cell1, int distance)
 *
 * Java draws from Math.random() (a JVM-global PRNG, NOT the engine's
 * deterministic LCG); we mirror that exactly via lw_java_math_random()
 * so existing seeds keep their sample order. */
LwCell* lw_map_get_random_cell_at_distance(LwMap *self, const LwCell *cell1, int distance);

/* public Cell getCellEqualDistance(State state) */
LwCell* lw_map_get_cell_equal_distance(LwMap *self, struct LwState *state);

/* public List<Cell> getCellsEqualDistance(Cell cell1, Cell cell2)
 *
 * Out-buffer form; returns count written. */
int lw_map_get_cells_equal_distance(LwMap *self,
                                    const LwCell *cell1,
                                    const LwCell *cell2,
                                    LwCell **out_buf, int out_cap);


/* ---- Distances and team helpers -------------------------------- */

/* public int getDistanceWithTeam(State state, int team, Cell cell) */
int lw_map_get_distance_with_team(LwMap *self, struct LwState *state, int team, const LwCell *cell);

/* public Cell getTeamBarycenter(State state, int team) */
LwCell* lw_map_get_team_barycenter(LwMap *self, struct LwState *state, int team);


/* ---- Composantes ----------------------------------------------- */

/* public void computeComposantes()
 *
 * BFS-style flood fill over coord[][] that assigns a connected-component
 * id to every cell.  Pure C version of the Java algorithm (same iteration
 * order, same merge logic). */
void lw_map_compute_composantes(LwMap *self);


/* ---- Attack helpers -------------------------------------------- */

/* public boolean canUseAttack(Cell caster, Cell target, Attack attack)
 * = verifyRange && verifyLoS */
int lw_map_can_use_attack(LwMap *self,
                          const LwCell *caster,
                          const LwCell *target,
                          const struct LwAttack *attack);

/* public boolean verifyRange(Cell caster, Cell target, Attack attack) */
int lw_map_verify_range(LwMap *self,
                        const LwCell *caster,
                        const LwCell *target,
                        const struct LwAttack *attack);


/* ---- Push / attract end-cells (called by Attack.applyOnCell) --- */

/* public Cell getPushLastAvailableCell(Cell entity, Cell target, Cell caster) */
LwCell* lw_map_get_push_last_available_cell(LwMap *self,
                                            LwCell *entity,
                                            LwCell *target,
                                            const LwCell *caster);

/* public Cell getAttractLastAvailableCell(Cell entity, Cell target, Cell caster) */
LwCell* lw_map_get_attract_last_available_cell(LwMap *self,
                                               LwCell *entity,
                                               LwCell *target,
                                               const LwCell *caster);


/* ---- Misc ------------------------------------------------------- */

/* public void clear() */
void lw_map_clear(LwMap *self);

/* public void positionChanged() -- the Java method clears a path cache
 * which the v2 engine doesn't keep. Stub kept for parity. */
static inline void lw_map_position_changed(LwMap *self) { (void)self; }


/* ---- Clone ------------------------------------------------------ */

/* public Map(Map map, State state) deep-copy ctor.
 *
 * All LwMap fields are POD (no pointers other than `state`), so a
 * single memcpy followed by `dst->state = new_state` matches the Java
 * deep copy semantics. */
void lw_map_clone(LwMap *dst, const LwMap *src, struct LwState *new_state);


/* ---- Forward-declared callbacks the engine must provide --------- */

/* These are needed by lw_map_generate_map and the random/team helpers
 * but live in modules that depend on Map.h (not the other way round).
 * Declared here so lw_map.c can reference them; defined in their
 * respective .c files (state, entity, team, leek). */

/* From lw_state: enumerate live entities of a team. Returns count;
 * writes up to `cap` entity-array-indices into `out`. */
int  lw_state_get_team_entity_count(const struct LwState *state, int team);
int  lw_state_get_team_entity      (const struct LwState *state, int team, int idx);

/* From lw_state: scenario type (LW_FIGHT_TYPE_*) and getEntity by fid. */
int  lw_state_get_type             (const struct LwState *state);
int  lw_state_get_entity_idx_by_fid(const struct LwState *state, int fid);

/* From lw_entity: liveness and type (LW_ENTITY_TYPE_*). */
int  lw_entity_is_dead             (const struct LwEntity *e);
int  lw_entity_get_type            (const struct LwEntity *e);
int  lw_entity_get_fid             (const struct LwEntity *e);
int  lw_entity_get_initial_cell    (const struct LwEntity *e);
int  lw_entity_get_cell_id         (const struct LwEntity *e);
void lw_entity_set_cell            (struct LwEntity *e, LwCell *cell);

/* From lw_state: lookup an entity record by entity-array-index. */
struct LwEntity* lw_state_get_entity(struct LwState *state, int entity_idx);

/* From lw_team: roster enumeration. */
int  lw_team_get_entity_count(const struct LwTeam *team);
int  lw_team_get_entity_idx  (const struct LwTeam *team, int idx);

/* From lw_attack: needLos / area / range queries used by canUseAttack.
 * Declarations live in lw_attack.h (some are static inline with int8_t
 * return), so we don't forward-declare them here -- include lw_attack.h
 * if you need them. */

/* From lw_los (declared elsewhere): full verifyLoS implementation. We
 * declare it here because canUseAttack calls it. */
int  lw_map_verify_los(LwMap *self,
                       const LwCell *start,
                       const LwCell *end,
                       const struct LwAttack *attack);

/* JVM Math.random (truly global, non-deterministic in Java) -- we use
 * a fixed sequence in C to keep parity with the recorded test seeds. */
double lw_java_math_random(void);


/* ---- Pathfinding (lw_pathfinding_astar.c) ----------------------- */

/* Java: public static double getDistance(Cell c1, Cell c2)
 *           = sqrt(getDistance2(c1, c2)) */
double lw_map_get_distance(const LwCell *c1, const LwCell *c2);

/* Java: public static int getDistance2(Cell c1, Cell c2)
 *           = (dx*dx + dy*dy) */
int    lw_map_get_distance2(const LwCell *c1, const LwCell *c2);

/* Java: public int getDistance2(Cell c1, List<Cell> cells)
 *           = min over cells of (dx*dx + dy*dy) */
int    lw_map_get_distance2_to_list(const LwCell *c1,
                                    LwCell *const *cells, int n_cells);

/* Java: public boolean available(Cell c, List<Cell> cells_to_ignore) */
int    lw_map_available_with_ignore(LwMap *self, LwCell *c,
                                    LwCell *const *cells_to_ignore,
                                    int n_cells_to_ignore);

/* Java: public List<Cell> getAStarPath(Cell c1, List<Cell> endCells,
 *                                       List<Cell> cells_to_ignore)
 *
 * Out-buf form. Returns:
 *   - n>=0: number of cells written into out_buf (path length)
 *   - -1   : no path found / invalid args
 *
 * `cells_to_ignore` may be NULL/0 (matches Java's null cells_to_ignore).
 * `targets` enumerates endCells (Java's overloaded varargs).
 */
int    lw_map_get_a_star_path(LwMap *self,
                              LwCell *from,
                              LwCell **targets, int n_targets,
                              LwCell **forbidden, int n_forbidden,
                              LwCell **out_buf, int out_cap);

/* Java: public List<Cell> getPathBeetween(Cell start, Cell end,
 *                                          List<Cell> cells_to_ignore)
 * (Java spelling: "Beetween" -- bug-compatible.)
 *
 * Out-buf form. Returns -1 if start/end is null or no path. */
int    lw_map_get_path_between(LwMap *self,
                               LwCell *start, LwCell *end,
                               LwCell **out_buf, int out_cap);

/* Java: public List<Cell> getValidCellsAroundObstacle(Cell cell)
 *
 * Out-buf form. Returns count written. */
int    lw_map_get_valid_cells_around_obstacle(LwMap *self,
                                              LwCell *cell,
                                              LwCell **out_buf, int out_cap);

/* Java: public Cell getFirstEntity(Cell from, Cell target,
 *                                   int minRange, int maxRange)
 * Returns the first cell with an entity along the line from -> target,
 * within [minRange,maxRange]. NULL if none. */
LwCell* lw_map_get_first_entity(LwMap *self,
                                LwCell *from, LwCell *target,
                                int min_range, int max_range);

/* Java: public List<Cell> getPathTowardLine(Cell start, Cell linecell1,
 *                                            Cell linecell2)
 * Out-buf form. Returns count written, or -1 if no path. */
int    lw_map_get_path_toward_line(LwMap *self,
                                   LwCell *start,
                                   LwCell *linecell1,
                                   LwCell *linecell2,
                                   LwCell **out_buf, int out_cap);

/* Java: public List<Cell> getPathAwayFromLine(Cell start, Cell linecell1,
 *                                              Cell linecell2, int max_distance)
 * Out-buf form. Returns count written, or -1 if no path. */
int    lw_map_get_path_away_from_line(LwMap *self,
                                      LwCell *start,
                                      LwCell *linecell1,
                                      LwCell *linecell2,
                                      int max_distance,
                                      LwCell **out_buf, int out_cap);

/* Java: public List<Cell> getPathAway(Cell start, List<Cell> bad_cells,
 *                                      int max_distance)
 * Out-buf form. Returns count written, or -1 if no path. */
int    lw_map_get_path_away(LwMap *self,
                            LwCell *start,
                            LwCell **bad_cells, int n_bad_cells,
                            int max_distance,
                            LwCell **out_buf, int out_cap);

/* Java: public List<Cell> getPathAwayMin(Map map, Cell start,
 *                                         List<Cell> bad_cells, int max_distance)
 * Same impl as getPathAway except passes `map` explicitly. */
int    lw_map_get_path_away_min(LwMap *self,
                                LwMap *map,
                                LwCell *start,
                                LwCell **bad_cells, int n_bad_cells,
                                int max_distance,
                                LwCell **out_buf, int out_cap);

/* Java: public List<Cell> getPossibleCastCellsForTarget(Attack attack,
 *                                                        Cell target,
 *                                                        List<Cell> cells_to_ignore)
 * Out-buf form. Returns count written. */
int    lw_map_get_possible_cast_cells_for_target(LwMap *self,
                                                 const struct LwAttack *attack,
                                                 LwCell *target,
                                                 LwCell **cells_to_ignore,
                                                 int n_cells_to_ignore,
                                                 LwCell **out_buf, int out_cap);

/* lw_attack_get_launch_type is static inline in lw_attack.h; include
 * that header from any caller that needs it. */


#endif /* LW_MAP_H */
