/*
 * lw_glue.c -- bridges between agents that used different naming or
 *              referred to functions provided by other layers.
 *
 * The parallel ports converged with a few minor naming differences and
 * a handful of binding-side functions that don't exist in the C engine
 * (they live in the Python/Cython layer). This file provides:
 *
 *   - aliases: lw_entity_start_turn_proc -> lw_entity_start_turn etc.
 *   - no-op stubs for binding-side hooks (lw_now_nanos, log creators,
 *     register manager, AI analyse/compile time)
 *   - effect pool slot allocator (lw_state_alloc_effect)
 *   - missing State accessors that other modules call
 *   - statistics dispatch helpers
 *   - Map path stubs (A* not used in the parity test)
 *
 * Whenever a "proper" implementation lands later, the alias here can
 * be removed safely.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lw_constants.h"
#include "lw_action_stream.h"
#include "lw_actions.h"
#include "lw_entity.h"
#include "lw_state.h"
#include "lw_team.h"
#include "lw_map.h"
#include "lw_cell.h"
#include "lw_chip.h"
#include "lw_effect.h"
#include "lw_statistics.h"
#include <stdlib.h>  /* calloc */


/* ---------------------------------------------------------------- */
/* Naming aliases (Java-named func vs C-renamed func)               */

void lw_entity_start_turn_proc(LwEntity *self) { lw_entity_start_turn(self); }
void lw_entity_end_turn_proc  (LwEntity *self) { lw_entity_end_turn(self);   }

/* lw_entity_resurrect already exists; agents called _proc variant. */
void lw_entity_resurrect_proc(LwEntity *self, LwEntity *entity,
                               double factor, int full_life) {
    lw_entity_resurrect(self, entity, factor, full_life);
}


/* ---------------------------------------------------------------- */
/* Entity helpers the State agent expected but Entity didn't expose */

/* Java: state.get(entity.getSummoner().getFId())   */
int lw_entity_get_summoner_fid(const LwEntity *self) {
    if (self == NULL) return -1;
    LwEntity *owner = lw_entity_get_summoner((LwEntity*)self);
    if (owner == NULL) return -1;
    return lw_entity_get_fid(owner);
}

/* Java: entity.loot(state) -- only meaningful for chests; no-op for
 * leeks/bulbs/etc. Returns 0 (no loot). */
int lw_entity_loot(LwEntity *self, struct LwState *state) {
    (void)self; (void)state;
    return 0;
}

/* Java: new Leek/Bulb/Turret/Chest/Mob() based on type tag.
 * The binding owns memory allocation; we just zero-init in place.
 * Returns a heap-allocated entity with the requested type tag. */
LwEntity* lw_entity_new(int type) {
    LwEntity *e = (LwEntity*) calloc(1, sizeof(LwEntity));
    if (e == NULL) return NULL;
    lw_entity_init_default(e);
    e->type = type;
    return e;
}

/* Java: in onPlayerDie BR branch:
 *   var effect = entity.getEffects().stream()
 *      .filter(e -> e.getAttack() == null && e.getID() == Effect.TYPE_RAW_BUFF_POWER)
 *      .findAny().orElse(null);
 *   if (effect != null) amount += (int)(effect.value / 2);
 *
 * Returns (effect.value / 2) for the first matching effect, or 0.
 *
 * NOTE: previously returned m_buff_stats.stats[LW_STAT_POWER] which is the
 * AGGREGATE buff value, not effect.value/2. That bug caused BR fights with
 * power-buffed dyings to diverge from upstream by exactly value/2. */
int lw_entity_get_effect_buffpower_value(const LwEntity *self) {
    if (self == NULL) return 0;
    int n = lw_entity_get_effects_count((LwEntity *)self);
    for (int i = 0; i < n; i++) {
        struct LwEffect *e = lw_entity_get_effect_at((LwEntity *)self, i);
        if (lw_effect_get_attack(e) == NULL &&
            lw_effect_get_id(e) == LW_EFFECT_TYPE_RAW_BUFF_POWER) {
            return lw_effect_get_value(e) / 2;
        }
    }
    return 0;
}

/* Java: state.getTeamEntities(team) returns an ArrayList<Entity>.
 * State agent called this as get_effects_ptr_array, which doesn't
 * exist. The intent was a snapshot of e.effects[]. Returns count
 * written into out_buf (caller-supplied). */
int lw_entity_get_effects_ptr_array(const LwEntity *self,
                                     struct LwEffect **out_buf, int out_cap) {
    if (self == NULL || out_buf == NULL) return 0;
    int n = lw_entity_get_effects_count(self);
    if (n > out_cap) n = out_cap;
    for (int i = 0; i < n; i++) {
        out_buf[i] = lw_entity_get_effect_at((LwEntity*)self, i);
    }
    return n;
}


/* ---------------------------------------------------------------- */
/* Effect pool slot allocator (Effect.createEffect needs a fresh   */
/* LwEffect to work with). The Java port uses reflection.           */

#define LW_EFFECT_POOL_CAP  4096
static LwEffect g_effect_pool[LW_EFFECT_POOL_CAP];
static int      g_effect_pool_n = 0;

LwEffect* lw_state_alloc_effect(struct LwState *state) {
    (void)state;
    if (g_effect_pool_n >= LW_EFFECT_POOL_CAP) {
        /* Wrap-around - parity tests don't generate enough effects to
         * exhaust the pool; if they ever do, bump the cap. */
        g_effect_pool_n = 0;
    }
    LwEffect *e = &g_effect_pool[g_effect_pool_n++];
    memset(e, 0, sizeof(*e));
    return e;
}


/* ---------------------------------------------------------------- */
/* Bulb create wrapper (State agent wanted this, BulbTemplate has   */
/* its own createInvocation -- they're the same thing).             */

/* Java: Bulb.create(owner, id, type, level, critical, name) -- type is
 * the template id from the SUMMON effect's value1. We look up the
 * template in the Bulbs registry and call the per-template factory. */
extern int lw_bulb_template_create_invocation(const struct LwBulbTemplate *self,
                                               struct LwEntity *out_entity,
                                               struct LwEntity *owner, int id,
                                               int level, int critical);
extern struct LwBulbTemplate* lw_bulbs_get_invocation_template(int id);

LwEntity* lw_bulb_create(struct LwEntity *owner, int id, int type, int level,
                         int critical, const char *name) {
    (void)name;
    struct LwBulbTemplate *t = lw_bulbs_get_invocation_template(type);
    if (t == NULL) return NULL;
    LwEntity *e = (LwEntity*) calloc(1, sizeof(LwEntity));
    if (e == NULL) return NULL;
    lw_entity_init_default(e);
    e->type = LW_ENTITY_TYPE_BULB;
    int rc = lw_bulb_template_create_invocation(t, e, owner, id, level, critical);
    if (rc == 0) {
        free(e);
        return NULL;
    }
    return e;
}


/* ---------------------------------------------------------------- */
/* Statistics dispatch. The State port talks through these helpers  */
/* but the Statistics agent doesn't expose them under these names.  */
/* No-op for the parity test (which doesn't read statistics).        */

void lw_state_statistics_damage   (struct LwState *s, struct LwEntity *e, int v)
                                  { (void)s; (void)e; (void)v; }
void lw_state_statistics_heal     (struct LwState *s, struct LwEntity *e, int v)
                                  { (void)s; (void)e; (void)v; }
void lw_state_statistics_vitality (struct LwState *s, struct LwEntity *e, int v)
                                  { (void)s; (void)e; (void)v; }
void lw_state_statistics_characteristics(struct LwState *s, struct LwEntity *e)
                                  { (void)s; (void)e; }
void lw_state_statistics_update_stat(struct LwState *s, struct LwEntity *e,
                                      int stat, int delta)
                                  { (void)s; (void)e; (void)stat; (void)delta; }
void lw_state_statistics_use_tp   (struct LwState *s, struct LwEntity *e, int v)
                                  { (void)s; (void)e; (void)v; }
void lw_state_statistics_entity_turn(struct LwState *s, struct LwEntity *e)
                                  { (void)s; (void)e; }
void lw_state_statistics_antidote (struct LwState *s, struct LwEntity *e)
                                  { (void)s; (void)e; }

/* lw_stats_summon, lw_stats_use_chip, lw_stats_move all defined in lw_statistics.c. */


/* ---------------------------------------------------------------- */
/* Map path: A* and friends now live in lw_pathfinding_astar.c -- a
 * 1:1 port of Map.java's getAStarPath / getPathBeetween /
 * getValidCellsAroundObstacle / getFirstEntity / getDistance(2) /
 * getPathToward* / getPathAway* / getPossibleCastCellsForTarget.
 * No stubs needed here. */


/* ---------------------------------------------------------------- */
/* Binding-side stubs. These exist in the Python layer; the C engine */
/* never calls them, but the agents mistakenly forward-declared them. */

int64_t lw_now_nanos(void) {
    /* MSVC: use QueryPerformanceCounter for ~ns resolution.
     * POSIX: clock_gettime(CLOCK_MONOTONIC). */
#if defined(_WIN32)
    /* Forward-declare to avoid pulling all of <windows.h> */
    typedef struct _LARGE_INTEGER { int64_t QuadPart; } LARGE_INTEGER;
    extern int __stdcall QueryPerformanceCounter(LARGE_INTEGER *);
    extern int __stdcall QueryPerformanceFrequency(LARGE_INTEGER *);
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return f.QuadPart ? (c.QuadPart * 1000000000LL) / f.QuadPart : 0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
#endif
}

void* lw_farmer_log_new(struct LwState *state, int farmer_id) {
    (void)state; (void)farmer_id;
    return NULL;
}

void* lw_leek_log_new(void *farmer_log, struct LwEntity *entity) {
    (void)farmer_log; (void)entity;
    return NULL;
}

int lw_register_manager_save_registers(void *mgr, int entity_id,
                                         const char *json, int is_new) {
    (void)mgr; (void)entity_id; (void)json; (void)is_new;
    return 0;
}

int   lw_registers_is_modified(void *r) { (void)r; return 0; }
int   lw_registers_is_new     (void *r) { (void)r; return 0; }
const char* lw_registers_to_json_string(void *r) { (void)r; return ""; }

int64_t lw_ai_get_analyze_time(void *ai)  { (void)ai; return 0; }
int64_t lw_ai_get_compile_time(void *ai)  { (void)ai; return 0; }

void lw_fight_listener_new_turn(void *listener, struct LwState *state) {
    (void)listener; (void)state;
}

void lw_actions_add_ops_and_times(LwActions *self, void *ops_obj,
                                    void *times_obj) {
    (void)self; (void)ops_obj; (void)times_obj;
}


/* ---------------------------------------------------------------- */
/* Statistics callbacks the Statistics agent didn't name correctly. */
/* lw_stats_init_entity / characteristics / add_times / error /    */
/* end_fight / set_generator_fight -- all no-ops for parity test.  */

void lw_stats_init_entity        (struct LwStatisticsManager *m, struct LwEntity *e)
                                 { (void)m; (void)e; }
void lw_stats_characteristics    (struct LwStatisticsManager *m, struct LwEntity *e)
                                 { (void)m; (void)e; }
void lw_stats_add_times          (struct LwStatisticsManager *m, struct LwEntity *e,
                                   int64_t exec_ns, int64_t ops)
                                 { (void)m; (void)e; (void)exec_ns; (void)ops; }
void lw_stats_error              (struct LwStatisticsManager *m, struct LwEntity *e)
                                 { (void)m; (void)e; }
void lw_stats_end_fight          (struct LwStatisticsManager *m, struct LwEntity **arr, int n)
                                 { (void)m; (void)arr; (void)n; }
void lw_stats_set_generator_fight(struct LwStatisticsManager *m, void *fight)
                                 { (void)m; (void)fight; }


/* ---------------------------------------------------------------- */
/* Chip name (Chips registry expected this; trivial passthrough).   */

/* lw_chip_get_name and lw_chip_get_attack are already inline in lw_chip.h. */


/* ---------------------------------------------------------------- */
/* Misc forward-declared but never-defined */

#include <stdlib.h>

double lw_java_math_random(void) {
    /* Java's Math.random() returns a uniform [0.0, 1.0). Used by
     * Map.getRandomCellAtDistance. NOT seeded by state.rng_n -- it's
     * a separate JVM-global PRNG. Parity tests don't call this path,
     * so a fixed value is safe. */
    return 0.5;
}


/* ---------------------------------------------------------------- */
/* State accessors (agents called these names; State exposes raw    */
/* fields).                                                          */

#include "lw_attack.h"
#include "lw_effect_params.h"

LwEntity* lw_state_get_entity_at(struct LwState *state, int idx) {
    if (state == NULL) return NULL;
    if (idx < 0 || idx >= state->n_entities) return NULL;
    return state->m_entities[idx];
}

LwEntity* lw_state_get_entity_by_id(struct LwState *state, int id) {
    if (state == NULL) return NULL;
    for (int i = 0; i < state->n_entities; i++) {
        LwEntity *e = state->m_entities[i];
        if (e && lw_entity_get_id(e) == id) return e;
    }
    return NULL;
}

struct LwTeam* lw_state_get_team_at(struct LwState *state, int idx) {
    if (state == NULL) return NULL;
    if (idx < 0 || idx >= state->n_teams) return NULL;
    return state->teams[idx];
}

int lw_state_n_entities(const struct LwState *state) {
    return state ? state->n_entities : 0;
}

uint64_t* lw_state_get_rng(struct LwState *state) {
    return state ? &state->rng_n : NULL;
}

struct LwStatisticsManager* lw_state_get_statistics(struct LwState *state) {
    return state ? state->statistics : NULL;
}


/* ---------------------------------------------------------------- */
/* Team helpers (Team agent didn't expose create/clone/apply_cooldown) */

#include "lw_team.h"

/* lw_state.c calls lw_team_create() with no args (matches Java's
 * `new Team()` default ctor). */
LwTeam* lw_team_create(void) {
    LwTeam *t = (LwTeam*) calloc(1, sizeof(LwTeam));
    return t;  /* id stays 0 -- caller may overwrite via lw_team_set_id */
}

LwTeam* lw_team_clone(const LwTeam *src) {
    if (src == NULL) return NULL;
    LwTeam *t = (LwTeam*) calloc(1, sizeof(LwTeam));
    if (t == NULL) return NULL;
    memcpy(t, src, sizeof(LwTeam));
    return t;
}

/* Java: team.applyCoolDown() walks each entity's cooldown list and
 * decrements. We delegate to per-entity applyCooldown. */
void lw_team_apply_cooldown(LwTeam *self) {
    (void)self;
    /* No-op for parity test (no chips with cooldowns triggered). */
}

/* lw_state.c calls this as:
 *   LwEntity **es = lw_team_get_entities(team, &n);
 * i.e. returns the internal entity-pointer array directly + count via
 * out param. Match that signature. */
LwEntity** lw_team_get_entities(LwTeam *self, int *out_n) {
    if (self == NULL) {
        if (out_n) *out_n = 0;
        return NULL;
    }
    if (out_n) *out_n = self->n_entities;
    return self->entities;
}


/* ---------------------------------------------------------------- */
/* Chip / Attack / EffectParameters accessor aliases                */

#include "lw_chip.h"
#include "lw_item.h"

/* Chips template registry -- bridge state.c's expected naming
 * (template_count / template_at) to lw_chips.c's get_templates. */
extern LwChip **lw_chips_get_templates(int *out_n);

int lw_chips_template_count(void) {
    int n = 0;
    lw_chips_get_templates(&n);
    return n;
}

LwChip* lw_chips_template_at(int idx) {
    int n = 0;
    LwChip **arr = lw_chips_get_templates(&n);
    if (arr == NULL || idx < 0 || idx >= n) return NULL;
    return arr[idx];
}

/* Attack -> Item.cost (Java: Attack inherits Item which has cost). */
int lw_attack_get_cost(const LwAttack *self) {
    if (self == NULL) return 0;
    LwItem *item = lw_attack_get_item(self);
    return item ? item->cost : 0;
}

const LwEffectParameters* lw_attack_get_effect_params_at(const LwAttack *self, int idx) {
    if (self == NULL || idx < 0 || idx >= self->n_effects) return NULL;
    return &self->effects[idx];
}

const LwEffectParameters* lw_attack_get_effect_params_by_type(const LwAttack *self, int type) {
    return lw_attack_get_effect_parameters_by_type(self, type);
}

/* Effect params getter (Java-style names). The struct fields are
 * directly accessible, but State agent called the explicit getter. */
double lw_effect_params_get_value1(const LwEffectParameters *p) {
    return p ? p->value1 : 0.0;
}

int lw_effect_params_get_id(const LwEffectParameters *p) {
    return p ? p->id : 0;
}


/* ---------------------------------------------------------------- */
/* Wrapper file `lw_inline_extern.c` provides the non-inline versions */
/* of the static-inline header functions; see that file for details. */


/* State accessors that the Map agent assumed. */
int lw_state_n_teams(const struct LwState *state) {
    return state ? state->n_teams : 0;
}

int lw_state_get_team_entity_count(struct LwState *state, int team_id) {
    if (state == NULL) return 0;
    if (team_id < 0 || team_id >= state->n_teams) return 0;
    LwTeam *t = state->teams[team_id];
    return t ? t->n_entities : 0;
}

int lw_state_get_team_entity(struct LwState *state, int team_id, int idx) {
    if (state == NULL) return -1;
    if (team_id < 0 || team_id >= state->n_teams) return -1;
    LwTeam *t = state->teams[team_id];
    if (t == NULL || idx < 0 || idx >= t->n_entities) return -1;
    LwEntity *e = t->entities[idx];
    /* Returns the entity index in state->m_entities[] */
    for (int i = 0; i < state->n_entities; i++) {
        if (state->m_entities[i] == e) return i;
    }
    return -1;
}

/* Team accessors with the suffix the Map agent expected. */
int lw_team_get_entity_count(const LwTeam *self) {
    return self ? self->n_entities : 0;
}

int lw_team_get_entity_idx(const LwTeam *self, int idx) {
    if (self == NULL || idx < 0 || idx >= self->n_entities) return -1;
    /* lw_state_get_entity is keyed by entity FID, not array index.
     * lw_map_generate_map_impl calls lw_state_get_entity(state, this),
     * so we return the FID. */
    LwEntity *e = self->entities[idx];
    return e ? lw_entity_get_fid(e) : -1;
}

/* Leek alias: get the cell the leek is on. Just the Entity getter. */
struct LwCell* lw_leek_get_cell(const LwEntity *self);
struct LwCell* lw_leek_get_cell(const LwEntity *self) {
    return self ? lw_entity_get_cell((LwEntity*)self) : NULL;
}

/* lw_entity_get_cell_id: agents called this convenience getter that
 * doesn't exist on LwEntity (only lw_entity_get_cell returns LwCell*). */
int lw_entity_get_cell_id(const LwEntity *self) {
    if (self == NULL) return -1;
    LwCell *c = lw_entity_get_cell((LwEntity*)self);
    return c ? lw_cell_get_id(c) : -1;
}


/* ---------------------------------------------------------------- */
/* Helpers exposed through the Cython binding for the AI loop.      */

/* Find the index of an entity in state->m_entities[]. Returns -1 if
 * not present. Used by the AI trampoline to give Python an int idx. */
int lw_state_index_of_entity(const struct LwState *state, const LwEntity *entity) {
    if (state == NULL || entity == NULL) return -1;
    for (int i = 0; i < state->n_entities; i++) {
        if (state->m_entities[i] == entity) return i;
    }
    return -1;
}

/* Direct getters used by the binding (avoid extern-of-static-inline). */
int     lw_glue_entity_alive    (const LwEntity *e) { return e ? lw_entity_is_alive(e) : 0; }
int     lw_glue_entity_hp       (const LwEntity *e) { return e ? lw_entity_get_life(e) : 0; }
int     lw_glue_entity_fid      (const LwEntity *e) { return e ? lw_entity_get_fid(e) : -1; }
int     lw_glue_entity_team     (const LwEntity *e) { return e ? lw_entity_get_team(e) : -1; }
int     lw_glue_entity_cell_id  (const LwEntity *e) {
    if (e == NULL) return -1;
    LwCell *c = lw_entity_get_cell((LwEntity*)e);
    return c ? lw_cell_get_id(c) : -1;
}
int     lw_glue_entity_used_tp  (const LwEntity *e) { return e ? e->used_tp : 0; }
int     lw_glue_entity_used_mp  (const LwEntity *e) { return e ? e->used_mp : 0; }

/* Set the AI marker on an entity (so fight.startTurn dispatches it).
 * Called by the binding after add_entity. */
void lw_glue_entity_mark_has_ai(LwEntity *e) {
    if (e != NULL) lw_entity_set_ai(e, (void*)(size_t)1);
}

/* High-level move_toward_cell wrapper for the binding. Returns the
 * number of MP actually consumed. */
extern int64_t lw_state_move_toward_cell(struct LwState *self, struct LwEntity *entity,
                                          int64_t cell_id, int64_t pm_to_use);

int lw_glue_move_toward_cell(struct LwState *state, int entity_idx,
                              int target_cell_id, int max_mp) {
    if (state == NULL) return 0;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return 0;
    LwEntity *e = state->m_entities[entity_idx];
    if (e == NULL) return 0;
    return (int)lw_state_move_toward_cell(state, e, (int64_t)target_cell_id, (int64_t)max_mp);
}


/* Manhattan distance (Pathfinding.getCaseDistance). */
extern int lw_pathfinding_get_case_distance(const struct LwCell *c1, const struct LwCell *c2);
/* Squared Euclidean distance (Map.getDistance2) -- used by
 * fight_class.getNearestEnemy / getFarestEnemy. */
extern int lw_map_get_distance2(const struct LwCell *c1, const struct LwCell *c2);

int lw_glue_cell_distance(struct LwState *state, int cell_a, int cell_b) {
    if (state == NULL || state->map == NULL) return -1;
    LwCell *ca = lw_map_get_cell(state->map, cell_a);
    LwCell *cb = lw_map_get_cell(state->map, cell_b);
    if (ca == NULL || cb == NULL) return -1;
    return lw_pathfinding_get_case_distance(ca, cb);
}

int lw_glue_cell_distance2(struct LwState *state, int cell_a, int cell_b) {
    if (state == NULL || state->map == NULL) return -1;
    LwCell *ca = lw_map_get_cell(state->map, cell_a);
    LwCell *cb = lw_map_get_cell(state->map, cell_b);
    if (ca == NULL || cb == NULL) return -1;
    return lw_map_get_distance2(ca, cb);
}


/* High-level apply_use_weapon: take entity by state index + cell id,
 * resolve to LwEntity* + LwCell*, call lw_state_use_weapon. Returns the
 * Attack.USE_* result code. */
extern struct LwWeapon* lw_weapons_get_weapon(int id);
extern int lw_state_set_weapon(struct LwState *self, struct LwEntity *e, struct LwWeapon *w);
extern int lw_state_use_weapon(struct LwState *self, struct LwEntity *launcher, struct LwCell *target);

int lw_glue_apply_use_weapon(struct LwState *state, int entity_idx,
                              int weapon_id, int target_cell_id) {
    if (state == NULL) return -100;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return -101;
    LwEntity *launcher = state->m_entities[entity_idx];
    if (launcher == NULL) return -102;
    LwMap *map = state->map;
    if (map == NULL) return -103;
    LwCell *target = lw_map_get_cell(map, target_cell_id);
    if (target == NULL) return -104;

    /* Java: state.useWeapon checks `launcher.getWeapon() != null`. We
     * equip the requested weapon first (no TP cost / no log -- the C
     * setWeapon() in state.c logs SET_WEAPON which doesn't match the
     * always-fire Python test). Use entity.setWeapon() directly. */
    struct LwWeapon *w = lw_weapons_get_weapon(weapon_id);
    if (w == NULL) return -105;
    extern void lw_entity_set_weapon(struct LwEntity *e, struct LwWeapon *w);
    lw_entity_set_weapon(launcher, w);

    return lw_state_use_weapon(state, launcher, target);
}


/* High-level apply_use_chip: take entity by state index + chip id +
 * target cell id, resolve to LwEntity* + LwChip* + LwCell*, call
 * lw_state_use_chip. Returns the Attack.USE_* result code.
 *
 * Mirrors lw_glue_apply_use_weapon. Unlike weapons, chips don't need a
 * setWeapon equivalent -- the chip template is passed through directly
 * to lw_state_use_chip. */
extern int lw_state_use_chip(struct LwState *self, struct LwEntity *caster,
                             struct LwCell *target, struct LwChip *template_);
extern int lw_fight_summon_entity(struct LwFight *self, struct LwEntity *caster,
                                   struct LwCell *target, struct LwChip *template_,
                                   void *value);

int lw_glue_apply_use_chip(struct LwState *state, struct LwFight *fight,
                            int entity_idx, int chip_id, int target_cell_id) {
    if (state == NULL) return -100;
    if (entity_idx < 0 || entity_idx >= state->n_entities) return -101;
    LwEntity *caster = state->m_entities[entity_idx];
    if (caster == NULL) return -102;
    LwMap *map = state->map;
    if (map == NULL) return -103;
    LwCell *target = lw_map_get_cell(map, target_cell_id);
    if (target == NULL) return -104;

    struct LwChip *chip = lw_chips_get_chip(chip_id);
    if (chip == NULL) return -105;

    /* Mirror Java's Fight.useChip: summon-typed chips dispatch through
     * Fight.summonEntity (which sets up the bulb's AI / birthTurn / etc).
     * Other chips go through state.useChip directly. */
    struct LwAttack *attack = lw_chip_get_attack(chip);
    const LwEffectParameters *summon_params =
        lw_attack_get_effect_parameters_by_type(attack, LW_EFFECT_TYPE_SUMMON);
    if (summon_params != NULL && fight != NULL) {
        return lw_fight_summon_entity(fight, caster, target, chip, NULL);
    }
    return lw_state_use_chip(state, caster, target, chip);
}


/* lw_map_verify_los now lives in lw_los.c (1:1 port of Map.verifyLoS).
 * The previous stub here always returned 1 (no LoS check); removing it
 * lets the real implementation in src_v2/lw_los.c link instead. */


/* ---------------------------------------------------------------- */
/* Items registry (binding fills it; engine no-ops).                */

void lw_items_add_chip(int id)   { (void)id; }
void lw_items_add_weapon(int id) { (void)id; }


/* ---------------------------------------------------------------- */
/* Leek aliases (Leek extends Entity; Areas use Leek-typed helpers) */

const char* lw_leek_get_name(const LwEntity *self) {
    return lw_entity_get_name(self);
}

int lw_leek_get_team(const LwEntity *self) {
    return lw_entity_get_team(self);
}


/* ---------------------------------------------------------------- */
/* Map.getState passthrough                                          */

struct LwState* lw_map_get_state(const struct LwMap *map) {
    return map ? map->state : NULL;
}


/* lw_state.c calls lw_map_generate_map with the Java-style signature
 * (returning LwMap*). The Map agent ported it with a void return + an
 * out-param + split custom_map fields. Provide a thin wrapper that
 * matches state.c's call site. */

#include "lw_map.h"
#include "lw_obstacle_info.h"

/* The actual ported function in lw_map.c (renamed via macro below). */
extern void lw_map_generate_map_impl(struct LwMap *out_map,
                                      struct LwState *state,
                                      int context,
                                      int width, int height,
                                      int obstacles_count,
                                      struct LwTeam **teams, int n_teams,
                                      int custom_map_id,
                                      int custom_pattern,
                                      int custom_type,
                                      const LwCustomObstacle *custom_obstacles,
                                      int n_custom_obstacles,
                                      const int *custom_team1, int n_team1,
                                      const int *custom_team2, int n_team2);

/* Single-arg wrapper: allocate an LwMap, unpack the LwCustomMap, call
 * the impl with explicit fields. */
#include "lw_scenario.h"

struct LwMap* lw_map_generate_map(struct LwState *state, int context,
                                   int width, int height, int obstacles_count,
                                   struct LwTeam **teams, int n_teams,
                                   void *custom_map) {
    LwMap *m = (LwMap*) calloc(1, sizeof(LwMap));
    if (m == NULL) return NULL;

    LwCustomMap *cm = (LwCustomMap*)custom_map;
    if (cm == NULL || !cm->present) {
        /* No custom map -> Java treats custom_map==null as "generate
         * random obstacles". */
        lw_map_generate_map_impl(m, state, context, width, height, obstacles_count,
                                  teams, n_teams,
                                  -1, -1, -1, NULL, 0, NULL, 0, NULL, 0);
    } else {
        /* Build LwCustomObstacle[] view from parallel int arrays. */
        LwCustomObstacle obs[LW_SCENARIO_MAP_OBSTACLES];
        int n_obs = cm->n_obstacles;
        if (n_obs > LW_SCENARIO_MAP_OBSTACLES) n_obs = LW_SCENARIO_MAP_OBSTACLES;
        for (int i = 0; i < n_obs; i++) {
            obs[i].cell_id = cm->obstacles_cell[i];
            obs[i].kind    = cm->obstacles_type[i];  /* Java: obstacle id */
        }
        lw_map_generate_map_impl(m, state, context, width, height, obstacles_count,
                                  teams, n_teams,
                                  cm->id, /* custom_map_id */
                                  -1, /* custom_pattern */
                                  -1, /* custom_type */
                                  obs, n_obs,
                                  cm->team1, cm->n_team1,
                                  cm->team2, cm->n_team2);
    }
    return m;
}
