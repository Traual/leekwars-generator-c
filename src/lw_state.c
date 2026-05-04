/*
 * lw_state.c -- 1:1 port of state/State.java
 *
 * The root of the engine.  Most other modules call into this file via
 * the LwState* root pointer.  Order of mutations / log calls / RNG
 * draws MUST match the Java source byte-for-byte (see
 * PORTING_CONVENTIONS.md "Order of operations is sacred").
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/State.java
 */
#include "lw_state.h"

#include "lw_rng.h"
#include "lw_util.h"
#include "lw_effect.h"
#include "lw_map.h"
#include "lw_team.h"
#include "lw_entity.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>     /* malloc/calloc/free */
#include <string.h>


/* Forward decl for the effect-pool allocator (defined in lw_glue.c).
 * Used by clone_remap_effect below. */
LwEffect* lw_state_alloc_effect(struct LwState *state);


/* ---- Forward declarations of cross-module helpers ------------------ *
 *
 * These live in lw_entity.c / lw_team.c / lw_map.c / lw_chip.c / etc.
 * (being ported in parallel).  We declare just the signatures the
 * State port needs.
 */

/* state/Entity.java */
int    lw_entity_get_fid          (const struct LwEntity *e);
int    lw_entity_get_id           (const struct LwEntity *e);
int    lw_entity_get_team         (const struct LwEntity *e);
int    lw_entity_get_type         (const struct LwEntity *e);
int    lw_entity_get_level        (const struct LwEntity *e);
int    lw_entity_get_skin         (const struct LwEntity *e);
int    lw_entity_get_hat          (const struct LwEntity *e);
int    lw_entity_get_agility      (const struct LwEntity *e);
int    lw_entity_get_tp           (const struct LwEntity *e);
int    lw_entity_get_mp           (const struct LwEntity *e);
int    lw_entity_get_frequency    (const struct LwEntity *e);
int    lw_entity_get_item_uses    (const struct LwEntity *e, int item_id);
int    lw_entity_is_dead          (const struct LwEntity *e);
int    lw_entity_is_alive         (const struct LwEntity *e);
int    lw_entity_is_summon        (const struct LwEntity *e);
int    lw_entity_get_resurrected  (const struct LwEntity *e);
struct LwCell* lw_entity_get_cell (const struct LwEntity *e);
struct LwWeapon* lw_entity_get_weapon(const struct LwEntity *e);
void   lw_entity_set_team         (struct LwEntity *e, int team);
void   lw_entity_set_state        (struct LwEntity *e, struct LwState *state, int fid);
void   lw_entity_set_weapon       (struct LwEntity *e, struct LwWeapon *w);
void   lw_entity_use_tp           (struct LwEntity *e, int amount);
void   lw_entity_use_mp           (struct LwEntity *e, int amount);
void   lw_entity_add_item_use     (struct LwEntity *e, int item_id);
void   lw_entity_start_turn_proc  (struct LwEntity *e);
void   lw_entity_end_turn_proc    (struct LwEntity *e);
void   lw_entity_on_critical      (struct LwEntity *e);
void   lw_entity_on_kill          (struct LwEntity *e);
void   lw_entity_on_ally_killed   (struct LwEntity *e);
void   lw_entity_on_moved         (struct LwEntity *e, struct LwEntity *caster);
void   lw_entity_resurrect_proc   (struct LwEntity *e, struct LwEntity *owner, double critical_factor, int full_life);
int    lw_entity_has_state        (const struct LwEntity *e, int state);
void   lw_entity_add_cooldown     (struct LwEntity *e, struct LwChip *chip, int cooldown);
int    lw_entity_has_cooldown     (const struct LwEntity *e, int chip_id);
int    lw_entity_get_cooldown     (const struct LwEntity *e, int chip_id);
struct LwEntity** lw_entity_get_effects_ptr_array(struct LwEntity *e, int *out_n);
int    lw_entity_loot             (struct LwEntity *e, struct LwState *state); /* returns 0; resources omitted */
int    lw_entity_get_effect_buffpower_value(struct LwEntity *e); /* sum of TYPE_RAW_BUFF_POWER from null-attack effects, /2 -- helper used by BR */

/* state/Team.java */
struct LwTeam* lw_team_create     (void);
/* lw_team_clone implementation lives in lw_glue.c with a single-arg
 * signature (the caller does the entity-pointer remapping). The decl
 * uses the typedef name to match lw_team.h. */
LwTeam* lw_team_clone             (const LwTeam *src);
void   lw_team_set_id             (struct LwTeam *t, int id);
int    lw_team_get_id             (const struct LwTeam *t);
void   lw_team_add_entity         (struct LwTeam *t, struct LwEntity *e);
void   lw_team_remove_entity      (struct LwTeam *t, struct LwEntity *e);
struct LwEntity** lw_team_get_entities(struct LwTeam *t, int *out_n);
int    lw_team_size               (const struct LwTeam *t);
int    lw_team_is_alive           (const struct LwTeam *t);
int    lw_team_is_dead            (const struct LwTeam *t);
int    lw_team_contains_chest     (const struct LwTeam *t);
int    lw_team_get_summon_count   (const struct LwTeam *t);
int    lw_team_get_life           (const struct LwTeam *t);
double lw_team_get_life_ratio     (const struct LwTeam *t);
void   lw_team_apply_cooldown     (struct LwTeam *t);
void   lw_team_add_cooldown       (struct LwTeam *t, struct LwChip *chip, int cooldown);
int    lw_team_has_cooldown       (const struct LwTeam *t, int chip_id);
int    lw_team_get_cooldown       (const struct LwTeam *t, int chip_id);
void   lw_team_add_flag           (struct LwTeam *t, int flag);

/* maps/Map.java */
struct LwMap* lw_map_generate_map (struct LwState *state, int context, int width, int height,
                                   int obstacle_count, struct LwTeam **teams, int n_teams,
                                   void *custom_map);
/* lw_map_clone signature lives in lw_map.h (3-arg in-place variant). */
struct LwCell* lw_map_get_cell    (const struct LwMap *m, int id);
int    lw_map_can_use_attack      (struct LwMap *m, struct LwCell *from, struct LwCell *to, struct LwAttack *atk);
int    lw_map_move_entity         (struct LwMap *m, struct LwEntity *e, struct LwCell *to);
void   lw_map_remove_entity       (struct LwMap *m, struct LwEntity *e);
void   lw_map_set_entity          (struct LwMap *m, struct LwEntity *e, struct LwCell *cell);
void   lw_map_invert_entities     (struct LwMap *m, struct LwEntity *a, struct LwEntity *b);
int    lw_map_get_path_between    (struct LwMap *m, struct LwCell *from, struct LwCell *to,
                                    struct LwCell **out_buf, int out_cap);
int    lw_map_get_a_star_path     (struct LwMap *m, struct LwCell *from, struct LwCell **targets, int n_targets,
                                    struct LwCell **forbidden, int n_forbidden,
                                    struct LwCell **out_buf, int out_cap);
int    lw_map_get_valid_cells_around_obstacle(struct LwMap *m, struct LwCell *cell,
                                               struct LwCell **out_buf, int out_cap);

/* maps/Cell.java */
int    lw_cell_get_id             (const struct LwCell *c);
int    lw_cell_is_walkable        (const struct LwCell *c);
int    lw_cell_available          (const struct LwCell *c, const struct LwMap *m);
struct LwEntity* lw_cell_get_player(const struct LwCell *c, const struct LwMap *m);

/* attack/Attack.java */
int    lw_attack_get_cost         (const struct LwAttack *a);
int    lw_attack_get_max_uses     (const struct LwAttack *a);
struct LwEffectParameters* lw_attack_get_effect_params_by_type(const struct LwAttack *a, int type);
double lw_effect_params_get_value1(const struct LwEffectParameters *p);
int    lw_effect_params_get_id    (const struct LwEffectParameters *p);
int    lw_attack_get_effects_count(const struct LwAttack *a);
struct LwEffectParameters* lw_attack_get_effect_params_at(const struct LwAttack *a, int i);
int    lw_attack_apply_on_cell    (struct LwAttack *a, struct LwState *state,
                                   struct LwEntity *launcher, struct LwCell *target, int critical,
                                   struct LwEntity **out_targets, int out_cap);

/* chips/Chip.java + weapons/Weapon.java -- canonical defs are static
 * inline in lw_chip.h / lw_weapon.h. Including the headers lets the
 * compiler inline the calls directly (no link-time symbol needed). */
#include "lw_chip.h"
#include "lw_weapon.h"
#include "lw_team.h"
#include "lw_actions.h"
#include <stdio.h>  /* snprintf */

/* chips/Chips.java -- enumeration of chip templates (defined in lw_glue.c). */
int    lw_chips_template_count    (void);
struct LwChip* lw_chips_template_at(int i);

/* entity/Bulb.java factory */
struct LwEntity* lw_bulb_create   (struct LwEntity *owner, int fid, int type, int level, int critical, const char *name);


/* ---- LeekDatas builder helper -------------------------------------- */

/* Java init() builds:
 *   ObjectNode list = Json.createObject();
 *   for (Entity l : mEntities.values()) {
 *       ArrayNode data = Json.createArray();
 *       data.add(l.getLevel());
 *       data.add(l.getSkin());
 *       if (l.getHat() > 0) data.add(l.getHat()); else data.addNull();
 *       list.set(String.valueOf(l.getId()), data);
 *   }
 *   mLeekDatas = list.toString();
 *
 * We mirror the JSON shape with snprintf into a fixed buffer.  Java's
 * HashMap iteration order is implementation-defined but deterministic
 * for a given insertion sequence on a given JVM.  We iterate in
 * insertion order (m_entities[] is insertion-ordered) which differs
 * from Java's HashMap order in general.  This field is only consumed
 * downstream as an opaque payload; parity tests don't compare it
 * directly. */
static void build_leek_datas(LwState *self) {
    char *p = self->m_leek_datas;
    char *end = self->m_leek_datas + sizeof(self->m_leek_datas);
    int n = snprintf(p, (size_t)(end - p), "{");
    if (n < 0) { *self->m_leek_datas = 0; return; }
    p += n;

    for (int i = 0; i < self->n_entities; i++) {
        struct LwEntity *l = self->m_entities[i];
        if (i > 0) {
            n = snprintf(p, (size_t)(end - p), ",");
            if (n < 0) break;
            p += n;
        }
        int hat = lw_entity_get_hat(l);
        if (hat > 0) {
            n = snprintf(p, (size_t)(end - p), "\"%d\":[%d,%d,%d]",
                         lw_entity_get_id(l), lw_entity_get_level(l), lw_entity_get_skin(l), hat);
        } else {
            n = snprintf(p, (size_t)(end - p), "\"%d\":[%d,%d,null]",
                         lw_entity_get_id(l), lw_entity_get_level(l), lw_entity_get_skin(l));
        }
        if (n < 0) break;
        p += n;
        if (p >= end - 2) break;
    }
    if (p < end - 1) {
        *p++ = '}';
        *p = 0;
    } else {
        self->m_leek_datas[sizeof(self->m_leek_datas) - 1] = 0;
    }
}


/* ---- Construction --------------------------------------------------- */

/* Java:
 *   public State() {
 *       teams = new ArrayList<Team>();
 *       initialOrder = new ArrayList<Entity>();
 *       actions = new Actions();
 *       mEntities = new HashMap<Integer, Entity>();
 *       type = TYPE_SOLO;
 *       context = CONTEXT_GARDEN;
 *       fullType = TYPE_SOLO_GARDEN;
 *       date = new Date();
 *   }
 */
void lw_state_init(LwState *self) {
    if (!self) return;

    self->rng_n              = 0;
    self->n_teams            = 0;
    self->n_initial_order    = 0;
    self->m_next_entity_id   = 0;
    self->m_winteam          = -1;
    self->n_entities         = 0;
    self->m_id               = 0;
    self->m_state            = LW_FIGHT_STATE_INIT;

    lw_order_init(&self->order);

    /* Java: "fullType = TYPE_SOLO_GARDEN" -- final field assigned in
     * the default ctor; matches LW_FIGHT_TYPE_SOLO_GARDEN style not in
     * lw_constants (Java's State.TYPE_SOLO_GARDEN = 1). */
    self->full_type          = 1;       /* TYPE_SOLO_GARDEN = 1 */
    self->m_start_farmer     = -1;
    self->last_turn          = 0;
    self->colossus_multiplier = 0;
    self->date               = 0;       /* binding owns wall-clock */
    self->map                = NULL;

    lw_actions_init(&self->actions);
    self->m_leek_datas[0]    = 0;

    self->context            = LW_CONTEXT_GARDEN;
    self->type               = LW_FIGHT_TYPE_SOLO;

    self->custom_map         = NULL;
    self->statistics         = NULL;
    self->register_manager   = NULL;
    self->execution_time     = 0;
    self->seed               = 0;

    self->draw_check_life    = 0;
    self->listener           = NULL;

    for (int i = 0; i < LW_STATE_MAX_TEAMS; i++) self->teams[i] = NULL;
    for (int i = 0; i < LW_STATE_MAX_ENTITIES; i++) {
        self->m_entities[i] = NULL;
        self->initial_order[i] = NULL;
    }
}


/* Java:
 *   public State(State state) {
 *       this.mId = state.mId;
 *       this.mState = state.mState;
 *       this.actions = new Actions();
 *       this.randomGenerator = state.randomGenerator;
 *       this.mEntities = new HashMap<>();
 *       for (entry : state.mEntities) { Leek copy; setState; mEntities.put(...); }
 *       for (entity : mEntities) { for (effect : src.entity.effects) { clone; setTarget/setCaster; addEffect; addLaunchedEffect; } }
 *       initialOrder = new ArrayList<>(); ...
 *       order = new Order(state.order, this);
 *       teams = new ArrayList<>(); for(...) teams.add(new Team(...));
 *       map = new Map(state.map, this);
 *       statistics = state.statistics;  registerManager = ...; ...
 *   }
 *
 * Deep snapshot of a fight state. The Cython binding allocates the
 * destination LwState, then calls into here to populate it. Caller
 * owns the destination memory; we own the heap blocks attached
 * (entities, teams, effects, map). lw_state_free_clone() releases
 * them.
 *
 * Pointer ownership and remapping rules:
 *   - LwWeapon, LwChip, LwAttack: GLOBAL registries, shared by all
 *     states. Pointers are copied as-is.
 *   - LwEntity: each one is heap-allocated per state; we calloc a
 *     fresh array and lw_entity_init_copy from src. Then we walk
 *     each new entity and remap any pointer that referenced the old
 *     state's objects (cell -> new map's same-id cell, m_owner -> new
 *     entity by fid, state -> new state).
 *   - LwEffect: pulled from a global pool (lw_state_alloc_effect);
 *     we re-allocate fresh slots for the clone and remap their
 *     caster/target/attack pointers.
 *   - LwMap, LwTeam: heap-allocated per state. Map cells are POD;
 *     entity_at_cell stores fids (stable across clones) so just
 *     memcpy. Team entities[] needs fid-based pointer fixup.
 *   - LwOrder: in-place struct on LwState; lw_order_copy already
 *     does the fid lookup against the destination state.
 *   - LwActions: in-place fixed-size struct; plain memcpy.
 */

/* Forward decls for helpers used below. */
static struct LwEntity* clone_state_lookup_entity_by_fid(LwState *state, int fid);
static struct LwCell*   clone_state_lookup_cell_by_id   (LwState *state, int cell_id);
static struct LwEffect* clone_remap_effect              (LwState *new_state, const struct LwEffect *src);


void lw_state_clone(LwState *self, const LwState *src) {
    if (!self || !src) return;

    /* Start from a clean slate: zero-init scalars + pointer slots. */
    lw_state_init(self);

    /* ---- 1. Scalars and embedded structs ----------------------------- */

    self->m_id                = src->m_id;
    self->m_state             = src->m_state;
    self->rng_n               = src->rng_n;
    self->statistics          = src->statistics;        /* shared (opaque) */
    self->register_manager    = src->register_manager;  /* shared (opaque) */
    self->full_type           = src->full_type;
    self->type                = src->type;
    self->context             = src->context;
    self->m_next_entity_id    = src->m_next_entity_id;
    self->m_winteam           = src->m_winteam;
    self->m_start_farmer      = src->m_start_farmer;
    self->last_turn           = src->last_turn;
    self->colossus_multiplier = src->colossus_multiplier;
    self->date                = src->date;
    memcpy(self->m_leek_datas, src->m_leek_datas, sizeof(self->m_leek_datas));
    self->execution_time      = src->execution_time;
    self->seed                = src->seed;
    self->draw_check_life     = src->draw_check_life;
    self->custom_map          = src->custom_map;        /* shared (opaque) */
    self->listener            = src->listener;          /* shared (opaque) */

    /* Action stream: fixed-size in-place struct. */
    memcpy(&self->actions, &src->actions, sizeof(LwActions));

    /* ---- 2. Allocate + deep-copy entities ---------------------------- */
    /* We need entities to exist before the map clone (so cell.entity_at_cell
     * fids resolve), before teams, and before order/initial_order remap.
     * lw_entity_init_copy copies the bulk of the entity but leaves
     * pointer fields (cell, m_owner, state, effects[]) referencing the
     * SOURCE state's objects -- we fix them up below. */
    self->n_entities = src->n_entities;
    for (int i = 0; i < src->n_entities; i++) {
        struct LwEntity *src_e = src->m_entities[i];
        if (!src_e) { self->m_entities[i] = NULL; continue; }
        struct LwEntity *new_e = (struct LwEntity*) calloc(1, sizeof(struct LwEntity));
        if (!new_e) { self->m_entities[i] = NULL; continue; }
        lw_entity_init_copy(new_e, src_e);
        self->m_entities[i] = new_e;
    }

    /* ---- 3. Allocate + clone the map -------------------------------- */
    /* lw_map_clone does memcpy + sets new_state. Cells are POD;
     * entity_at_cell holds fids, stable across clones. */
    if (src->map) {
        struct LwMap *new_map = (struct LwMap*) calloc(1, sizeof(struct LwMap));
        if (new_map) {
            lw_map_clone(new_map, src->map, self);
            self->map = new_map;
        }
    }

    /* ---- 4. Remap entity pointer fields to the new state ------------ */
    /* Each cloned entity's `cell` field still points into the old map;
     * remap to the new map's cell with the same id. m_owner (Bulb's
     * summoner) still points at the old entity; remap by fid. state
     * also needs to point at the new self. */
    for (int i = 0; i < self->n_entities; i++) {
        struct LwEntity *e = self->m_entities[i];
        if (!e) continue;
        struct LwEntity *src_e = src->m_entities[i];

        /* state pointer */
        e->state = self;

        /* cell pointer */
        if (src_e && src_e->cell) {
            e->cell = clone_state_lookup_cell_by_id(self, src_e->cell->id);
        } else {
            e->cell = NULL;
        }

        /* m_owner (bulb summoner) */
        if (src_e && src_e->m_owner) {
            int owner_fid = lw_entity_get_fid(src_e->m_owner);
            e->m_owner = clone_state_lookup_entity_by_fid(self, owner_fid);
        } else {
            e->m_owner = NULL;
        }

        /* effects[] / launched_effects[]: lw_entity_init_copy explicitly
         * does NOT copy these (Java behavior). They start empty here.
         * We fill them in below from the source's effects, with
         * caster/target pointer remapping. */
        e->n_effects = 0;
        e->n_launched_effects = 0;

        /* AI / logs / fight / ai_file / m_register: opaque; the
         * lw_entity_init_copy does NOT copy these. They start NULL,
         * which matches Java's `new Leek(src)` behavior of leaving
         * them unset on the copy. The fight runner re-attaches them
         * via Entity.setState/setAI hooks. For replay/MCTS, we don't
         * call those hooks again; the engine reads them via
         * lw_entity_get_ai etc. so they need to be valid pointers or
         * NULL.  Leaving NULL is safe -- AI dispatch code path
         * already checks for NULL and is a no-op when not set. */
    }

    /* ---- 5. Re-clone effects on each entity ------------------------- */
    /* Walk the source's effects[] / launched_effects[] for each entity
     * and create fresh effect slots in the clone, with caster/target
     * remapped to the new state's entities. */
    for (int i = 0; i < src->n_entities; i++) {
        struct LwEntity *src_e = src->m_entities[i];
        struct LwEntity *new_e = self->m_entities[i];
        if (!src_e || !new_e) continue;

        /* effects[] -- effects acting ON this entity */
        for (int k = 0; k < src_e->n_effects; k++) {
            struct LwEffect *new_eff = clone_remap_effect(self, src_e->effects[k]);
            if (new_eff && new_e->n_effects < LW_ENTITY_MAX_EFFECTS) {
                new_e->effects[new_e->n_effects++] = new_eff;
            }
        }
        /* launched_effects[] -- effects this entity launched on others */
        for (int k = 0; k < src_e->n_launched_effects; k++) {
            struct LwEffect *new_eff = clone_remap_effect(self, src_e->launched_effects[k]);
            if (new_eff && new_e->n_launched_effects < LW_ENTITY_MAX_LAUNCHED_EFFECTS) {
                new_e->launched_effects[new_e->n_launched_effects++] = new_eff;
            }
        }
    }

    /* ---- 6. Allocate + remap teams ---------------------------------- */
    /* lw_team_clone does memcpy of the team struct but its entities[]
     * array still holds OLD entity pointers. We patch those by fid
     * lookup against the new state. */
    self->n_teams = src->n_teams;
    for (int i = 0; i < src->n_teams; i++) {
        if (!src->teams[i]) { self->teams[i] = NULL; continue; }
        LwTeam *new_team = lw_team_clone(src->teams[i]);
        if (!new_team) { self->teams[i] = NULL; continue; }
        /* src->teams[i] is `struct LwTeam *` per the in-state declaration
         * but the typedef LwTeam is what carries the field layout; cast
         * the source pointer through the typedef so MSVC accepts the
         * member access. */
        LwTeam *src_team = (LwTeam *) src->teams[i];
        for (int k = 0; k < new_team->n_entities; k++) {
            LwEntity *src_member = src_team->entities[k];
            if (src_member) {
                new_team->entities[k] = clone_state_lookup_entity_by_fid(
                    self, lw_entity_get_fid(src_member));
            } else {
                new_team->entities[k] = NULL;
            }
        }
        self->teams[i] = new_team;
    }

    /* ---- 7. Initial order: same-fid lookup ------------------------- */
    self->n_initial_order = src->n_initial_order;
    for (int i = 0; i < src->n_initial_order; i++) {
        struct LwEntity *src_e = src->initial_order[i];
        if (src_e) {
            self->initial_order[i] = clone_state_lookup_entity_by_fid(
                self, lw_entity_get_fid(src_e));
        } else {
            self->initial_order[i] = NULL;
        }
    }

    /* ---- 8. Order: helper does fid-based remap --------------------- */
    /* lw_order_copy iterates src.leeks[], looks up each by fid in
     * `state` (which we pass as `self`), so this works only after
     * step 2 populated self->m_entities. */
    lw_order_copy(&self->order, &src->order, self);
    /* lw_order_copy resets `turn` to 1 to mirror a Java bug; restore
     * the actual turn so MCTS rollouts continue from the snapshot. */
    self->order.turn = src->order.turn;
}


/* Helper: same as lw_state_get_entity but inlined for clone use. */
static struct LwEntity* clone_state_lookup_entity_by_fid(LwState *state, int fid) {
    if (!state) return NULL;
    for (int i = 0; i < state->n_entities; i++) {
        if (state->m_entities[i] && lw_entity_get_fid(state->m_entities[i]) == fid) {
            return state->m_entities[i];
        }
    }
    return NULL;
}

/* Helper: cells live inline in map.cells[]; lookup is O(1) by id. */
static struct LwCell* clone_state_lookup_cell_by_id(LwState *state, int cell_id) {
    if (!state || !state->map) return NULL;
    if (cell_id < 0 || cell_id >= LW_MAP_MAX_CELLS) return NULL;
    return &state->map->cells[cell_id];
}

/* Helper: clone one effect into the new state's pool, remapping
 * caster/target/attack pointers via the new state's entity table.
 * Returns NULL if the source effect is NULL or pool allocation
 * fails. */
static struct LwEffect* clone_remap_effect(LwState *new_state, const struct LwEffect *src) {
    if (!src || !new_state) return NULL;
    struct LwEffect *dst = lw_state_alloc_effect(new_state);
    if (!dst) return NULL;
    /* Plain memcpy of the effect struct. */
    memcpy(dst, src, sizeof(LwEffect));
    /* Remap caster/target. attack is a global registry pointer
     * (LwWeapon/LwChip's bundled LwAttack), so it stays as-is. */
    if (src->caster) {
        dst->caster = clone_state_lookup_entity_by_fid(
            new_state, lw_entity_get_fid(src->caster));
    }
    if (src->target) {
        dst->target = clone_state_lookup_entity_by_fid(
            new_state, lw_entity_get_fid(src->target));
    }
    return dst;
}


/* Free the heap allocations attached to a cloned state. Call this
 * only on states produced by lw_state_clone -- never on states owned
 * by a Generator (those are freed by lw_generator_dispose).
 *
 * Effects come from the global pool, so we don't free them
 * individually -- the pool wraps around. Map and entities are heap
 * blocks we owned. Teams ditto. */
void lw_state_free_clone(LwState *self) {
    if (!self) return;
    if (self->map) {
        free(self->map);
        self->map = NULL;
    }
    for (int i = 0; i < self->n_entities; i++) {
        if (self->m_entities[i]) {
            free(self->m_entities[i]);
            self->m_entities[i] = NULL;
        }
    }
    self->n_entities = 0;
    for (int i = 0; i < self->n_teams; i++) {
        if (self->teams[i]) {
            free(self->teams[i]);
            self->teams[i] = NULL;
        }
    }
    self->n_teams = 0;
    /* effects[] live in the global pool; nothing to free. */
}


/* ---- Trivial accessors --------------------------------------------- */

void lw_state_seed(LwState *self, int seed) {
    self->seed = seed;
    lw_rng_seed(&self->rng_n, (int64_t)seed);
}

uint64_t* lw_state_get_random(LwState *self) { return &self->rng_n; }

struct LwMap*    lw_state_get_map     (LwState *self)         { return self->map; }
LwActions*       lw_state_get_actions (LwState *self)         { return &self->actions; }
LwOrder*         lw_state_get_order   (LwState *self)         { return &self->order; }

int  lw_state_get_state    (const LwState *self) { return self->m_state; }
void lw_state_set_state    (LwState *self, int state) { self->m_state = state; }

int  lw_state_get_turn     (const LwState *self) { return ((LwState*)self)->order.turn; }
int  lw_state_get_max_turns(const LwState *self) { (void)self; return LW_FIGHT_MAX_TURNS; }

int  lw_state_get_type     (const LwState *self) { return self->type; }
int  lw_state_get_context  (const LwState *self) { return self->context; }
int  lw_state_get_full_type(const LwState *self) { return self->full_type; }

void lw_state_set_type     (LwState *self, int type)    { self->type = type; }
void lw_state_set_context  (LwState *self, int ctx)     { self->context = ctx; }

void lw_state_set_id       (LwState *self, int id) { self->m_id = id; }
int  lw_state_get_id       (const LwState *self) { return self->m_id; }

void lw_state_set_statistics_manager(LwState *self, LwStatisticsManager *m) { self->statistics = m; }
void  lw_state_set_register_manager (LwState *self, void *rm)               { self->register_manager = rm; }
void* lw_state_get_register_manager (LwState *self)                          { return self->register_manager; }

int  lw_state_get_winner   (const LwState *self) { return self->m_winteam; }
void lw_state_set_start_farmer(LwState *self, int v) { self->m_start_farmer = v; }
int  lw_state_get_start_farmer(const LwState *self) { return self->m_start_farmer; }

int  lw_state_get_duration (const LwState *self) { return ((LwState*)self)->order.turn; }

int64_t lw_state_get_seed  (const LwState *self) { return (int64_t)self->seed; }

const char* lw_state_get_leek_datas(const LwState *self) { return self->m_leek_datas; }

void lw_state_set_custom_map(LwState *self, void *map) { self->custom_map = map; }


/* Java: public Entity getLastEntity() { return mEntities.get(mNextEntityId - 1); } */
struct LwEntity* lw_state_get_last_entity(LwState *self) {
    return lw_state_get_entity(self, self->m_next_entity_id - 1);
}


/* ---- Entity / Team management -------------------------------------- */

/* Java: public int getNextEntityId() { int id = mNextEntityId; mNextEntityId++; return id; } */
int lw_state_get_next_entity_id(LwState *self) {
    int id = self->m_next_entity_id;
    self->m_next_entity_id++;
    return id;
}


/* Java:
 *   public void addEntity(int t, Entity entity) {
 *       if (entity == null || t < 0) return;
 *       int team = t;
 *       if (type == State.TYPE_BATTLE_ROYALE) {
 *           team = teams.size();
 *       }
 *       while (teams.size() < team + 1) {
 *           teams.add(new Team());
 *       }
 *       entity.setTeam(team);
 *       teams.get(team).addEntity(entity);
 *       entity.setState(this, getNextEntityId());
 *       mEntities.put(entity.getFId(), entity);
 *   }
 */
void lw_state_add_entity(LwState *self, int t, struct LwEntity *entity) {
    if (entity == NULL || t < 0) {
        return;
    }
    int team = t;
    if (self->type == LW_FIGHT_TYPE_BATTLE_ROYALE) {
        team = self->n_teams;
    }
    while (self->n_teams < team + 1) {
        if (self->n_teams >= LW_STATE_MAX_TEAMS) return;
        self->teams[self->n_teams] = lw_team_create();
        self->n_teams++;
    }
    lw_entity_set_team(entity, team);
    lw_team_add_entity(self->teams[team], entity);
    lw_entity_set_state(entity, self, lw_state_get_next_entity_id(self));

    if (self->n_entities < LW_STATE_MAX_ENTITIES) {
        self->m_entities[self->n_entities++] = entity;
    }
}


/* Java: public Entity getEntity(int id) { return mEntities.get(id); } */
struct LwEntity* lw_state_get_entity(LwState *self, int id) {
    if (!self) return NULL;
    /* Java HashMap.get(id) -- linear scan over our flat insertion-ordered
     * array (entity count is small, order is bounded). */
    for (int i = 0; i < self->n_entities; i++) {
        if (lw_entity_get_fid(self->m_entities[i]) == id) {
            return self->m_entities[i];
        }
    }
    return NULL;
}


/* Java: public java.util.Map<Integer, Entity> getEntities() { return mEntities; } */
struct LwEntity** lw_state_get_entities(LwState *self, int *out_n) {
    if (out_n) *out_n = self->n_entities;
    return self->m_entities;
}


/* Java:
 *   public List<Entity> getEnemiesEntities(int team) {
 *       return getEnemiesEntities(team, false);
 *   }
 */
int lw_state_get_enemies_entities(LwState *self, int team,
                                  struct LwEntity **out_buf, int out_cap) {
    return lw_state_get_enemies_entities_ex(self, team, 0, out_buf, out_cap);
}


/* Java:
 *   public List<Entity> getEnemiesEntities(int team, boolean get_deads) {
 *       List<Entity> enemies = new ArrayList<>();
 *       for (int t = 0; t < teams.size(); ++t) {
 *           if (t != team) {
 *               enemies.addAll(getTeamEntities(t, get_deads));
 *           }
 *       }
 *       return enemies;
 *   }
 */
int lw_state_get_enemies_entities_ex(LwState *self, int team, int get_deads,
                                     struct LwEntity **out_buf, int out_cap) {
    int n_out = 0;
    for (int t = 0; t < self->n_teams; t++) {
        if (t != team) {
            struct LwEntity *tmp[LW_STATE_MAX_ENTITIES];
            int n = lw_state_get_team_entities_ex(self, t, get_deads, tmp, LW_STATE_MAX_ENTITIES);
            for (int i = 0; i < n; i++) {
                if (n_out < out_cap) out_buf[n_out] = tmp[i];
                n_out++;
            }
        }
    }
    return n_out;
}


/* Java:
 *   public List<Entity> getTeamEntities(int team) { return getTeamEntities(team, false); }
 *   public List<Entity> getTeamEntities(int team, boolean dead) {
 *       List<Entity> leeks = new ArrayList<>();
 *       if (team < teams.size()) {
 *           for (Entity e : teams.get(team).getEntities()) {
 *               if (dead || !e.isDead()) leeks.add(e);
 *           }
 *       }
 *       return leeks;
 *   }
 */
int lw_state_get_team_entities(LwState *self, int team,
                               struct LwEntity **out_buf, int out_cap) {
    return lw_state_get_team_entities_ex(self, team, 0, out_buf, out_cap);
}

int lw_state_get_team_entities_ex(LwState *self, int team, int dead,
                                  struct LwEntity **out_buf, int out_cap) {
    int n_out = 0;
    if (team < self->n_teams) {
        int n;
        struct LwEntity **all = lw_team_get_entities(self->teams[team], &n);
        for (int i = 0; i < n; i++) {
            struct LwEntity *e = all[i];
            if (dead || !lw_entity_is_dead(e)) {
                if (n_out < out_cap) out_buf[n_out] = e;
                n_out++;
            }
        }
    }
    return n_out;
}


/* Java:
 *   public List<Entity> getTeamLeeks(int team) {
 *       List<Entity> leeks = new ArrayList<>();
 *       if (team < teams.size()) {
 *           for (Entity e : teams.get(team).getEntities()) {
 *               if (!e.isDead() && e.getType() == Entity.TYPE_LEEK) leeks.add(e);
 *           }
 *       }
 *       return leeks;
 *   }
 */
int lw_state_get_team_leeks(LwState *self, int team,
                            struct LwEntity **out_buf, int out_cap) {
    int n_out = 0;
    if (team < self->n_teams) {
        int n;
        struct LwEntity **all = lw_team_get_entities(self->teams[team], &n);
        for (int i = 0; i < n; i++) {
            struct LwEntity *e = all[i];
            if (!lw_entity_is_dead(e) && lw_entity_get_type(e) == LW_ENTITY_TYPE_LEEK) {
                if (n_out < out_cap) out_buf[n_out] = e;
                n_out++;
            }
        }
    }
    return n_out;
}


/* Java:
 *   public List<Entity> getAllEntities(boolean get_deads) {
 *       List<Entity> leeks = new ArrayList<>();
 *       for (Team t : teams) {
 *           for (Entity e : t.getEntities()) {
 *               if (get_deads || !e.isDead()) leeks.add(e);
 *           }
 *       }
 *       return leeks;
 *   }
 */
int lw_state_get_all_entities(LwState *self, int get_deads,
                              struct LwEntity **out_buf, int out_cap) {
    int n_out = 0;
    for (int t = 0; t < self->n_teams; t++) {
        int n;
        struct LwEntity **all = lw_team_get_entities(self->teams[t], &n);
        for (int i = 0; i < n; i++) {
            struct LwEntity *e = all[i];
            if (get_deads || !lw_entity_is_dead(e)) {
                if (n_out < out_cap) out_buf[n_out] = e;
                n_out++;
            }
        }
    }
    return n_out;
}


/* Java: public List<Team> getTeams() { return teams; } */
struct LwTeam** lw_state_get_teams(LwState *self, int *out_n) {
    if (out_n) *out_n = self->n_teams;
    return self->teams;
}


/* Java:
 *   public void setTeamID(int team, int id) {
 *       if (team < teams.size()) teams.get(team).setID(id);
 *   }
 */
void lw_state_set_team_id(LwState *self, int team, int id) {
    if (team < self->n_teams) {
        lw_team_set_id(self->teams[team], id);
    }
}


/* Java:
 *   public int getTeamID(int team) {
 *       if (team < teams.size()) return teams.get(team).getID();
 *       return -1;
 *   }
 */
int lw_state_get_team_id(LwState *self, int team) {
    if (team < self->n_teams) {
        return lw_team_get_id(self->teams[team]);
    }
    return -1;
}


/* Java: public void addFlag(int team, int flag) { teams.get(team).addFlag(flag); } */
void lw_state_add_flag(LwState *self, int team, int flag) {
    if (team < self->n_teams) {
        lw_team_add_flag(self->teams[team], flag);
    }
}


/* ---- Action stream shortcut ---------------------------------------- */

/* Java: public void log(Action log) { actions.log(log); } */
void lw_state_log(LwState *self, const LwActionLog *entry) {
    lw_actions_log(&self->actions, entry);
}


/* ---- Cooldowns ----------------------------------------------------- */

/* Java: public void addCooldown(Entity entity, Chip chip) { addCooldown(entity, chip, chip.getCooldown()); } */
void lw_state_add_cooldown_default(LwState *self, struct LwEntity *entity, struct LwChip *chip) {
    if (chip == NULL) return;
    lw_state_add_cooldown(self, entity, chip, lw_chip_get_cooldown(chip));
}

/* Java:
 *   public void addCooldown(Entity entity, Chip chip, int cooldown) {
 *       if (chip == null) return;
 *       if (chip.isTeamCooldown()) teams.get(entity.getTeam()).addCooldown(chip, cooldown);
 *       else                       entity.addCooldown(chip, cooldown);
 *   }
 */
void lw_state_add_cooldown(LwState *self, struct LwEntity *entity, struct LwChip *chip, int cooldown) {
    if (chip == NULL) return;
    if (lw_chip_is_team_cooldown(chip)) {
        lw_team_add_cooldown(self->teams[lw_entity_get_team(entity)], chip, cooldown);
    } else {
        lw_entity_add_cooldown(entity, chip, cooldown);
    }
}


/* Java:
 *   public boolean hasCooldown(Entity entity, Chip chip) {
 *       if (chip == null) return false;
 *       if (chip.isTeamCooldown()) return teams.get(entity.getTeam()).hasCooldown(chip.getId());
 *       else                       return entity.hasCooldown(chip.getId());
 *   }
 */
int lw_state_has_cooldown(LwState *self, struct LwEntity *entity, struct LwChip *chip) {
    if (chip == NULL) return 0;
    if (lw_chip_is_team_cooldown(chip)) {
        return lw_team_has_cooldown(self->teams[lw_entity_get_team(entity)], lw_chip_get_id(chip));
    } else {
        return lw_entity_has_cooldown(entity, lw_chip_get_id(chip));
    }
}


/* Java:
 *   public int getCooldown(Entity entity, Chip chip) {
 *       if (chip == null) return 0;
 *       if (chip.isTeamCooldown()) return teams.get(entity.getTeam()).getCooldown(chip.getId());
 *       else                       return entity.getCooldown(chip.getId());
 *   }
 */
int lw_state_get_cooldown(LwState *self, struct LwEntity *entity, struct LwChip *chip) {
    if (chip == NULL) return 0;
    if (lw_chip_is_team_cooldown(chip)) {
        return lw_team_get_cooldown(self->teams[lw_entity_get_team(entity)], lw_chip_get_id(chip));
    } else {
        return lw_entity_get_cooldown(entity, lw_chip_get_id(chip));
    }
}


/* ---- Critical generation ------------------------------------------- */

/* Java:
 *   public boolean generateCritical(Entity caster) {
 *       return getRandom().getDouble() < ((double) caster.getAgility() / 1000);
 *   }
 *
 * NOTE: 1 RNG draw, ALWAYS at this exact spot (sacred -- see PORTING_CONVENTIONS.md).
 */
int lw_state_generate_critical(LwState *self, struct LwEntity *caster) {
    return lw_rng_get_double(&self->rng_n) < ((double)lw_entity_get_agility(caster) / 1000.0) ? 1 : 0;
}


/* ---- Init ---------------------------------------------------------- */

/* Java: public void init()
 *
 * (THE BIG ONE.  Order is sacred: build leekDatas; getRandom().getInt(30,80)
 * for obstacle count; Map.generateMap(); StartOrder + Order setup;
 * compute; loop entities (alive→addEntity, addEntity to actions);
 * actions.addMap; cooldowns from chip templates; ActionStartFight;
 * Colossus init effect; transition to RUNNING.) */
void lw_state_init_fight(LwState *self) {

    // Create level/skin list
    build_leek_datas(self);

    int obstacle_count = lw_rng_get_int(&self->rng_n, 30, 80);

    self->map = lw_map_generate_map(self, self->context, 18, 18, obstacle_count,
                                    self->teams, self->n_teams, self->custom_map);

    // Initialize positions and game order
    LwStartOrder bootorder;
    lw_start_order_init(&bootorder);
    lw_order_init(&self->order);

    for (int t_i = 0; t_i < self->n_teams; t_i++) {
        int n;
        struct LwEntity **es = lw_team_get_entities(self->teams[t_i], &n);
        for (int i = 0; i < n; i++) {
            lw_start_order_add_entity(&bootorder, es[i]);
        }
    }

    struct LwEntity *order_buf[LW_STATE_MAX_ENTITIES];
    int n_order = lw_start_order_compute(&bootorder, self, order_buf, LW_STATE_MAX_ENTITIES);

    for (int i = 0; i < n_order; i++) {
        struct LwEntity *e = order_buf[i];
        if (lw_entity_is_alive(e)) {
            lw_order_add_entity(&self->order, e);
        }
        lw_actions_add_entity(&self->actions, e, 0);
        if (self->n_initial_order < LW_STATE_MAX_ENTITIES) {
            self->initial_order[self->n_initial_order++] = e;
        }

        // Coffre ?
        if (lw_entity_get_type(e) == LW_ENTITY_TYPE_CHEST) {
            lw_stats_chest(self->statistics);
        }
    }

    // On ajoute la map
    lw_actions_add_map(&self->actions, self->map);

    // Cooldowns initiaux
    int n_chips = lw_chips_template_count();
    for (int i = 0; i < n_chips; i++) {
        struct LwChip *chip = lw_chips_template_at(i);
        if (chip == NULL) continue;
        if (lw_chip_get_initial_cooldown(chip) > 0) {
            for (int t_i = 0; t_i < self->n_teams; t_i++) {
                int n;
                struct LwEntity **es = lw_team_get_entities(self->teams[t_i], &n);
                for (int j = 0; j < n; j++) {
                    lw_state_add_cooldown(self, es[j], chip, lw_chip_get_initial_cooldown(chip) + 1);
                }
            }
        }
    }

    // Puis on ajoute le startfight
    int t1 = self->n_teams > 0 ? lw_team_size(self->teams[0]) : 0;
    int t2 = self->n_teams > 1 ? lw_team_size(self->teams[1]) : 0;
    lw_actions_log_start_fight(&self->actions, t1, t2);

    // Colossus: apply initial x3 multiply stats effect on team 2 (the colossus)
    // Must be after ActionStartFight so the client processes it
    // stackable=false so each turn's new effect replaces the previous one
    if (self->type == LW_FIGHT_TYPE_COLOSSUS && self->n_teams > 1) {
        self->colossus_multiplier = 3;
        int n;
        struct LwEntity **es = lw_team_get_entities(self->teams[1], &n);
        for (int i = 0; i < n; i++) {
            struct LwEntity *e = es[i];
            lw_effect_create_effect(self, LW_EFFECT_TYPE_MULTIPLY_STATS, -1, 1.0,
                                    (double)self->colossus_multiplier, 0.0, 0,
                                    e, e, NULL, 0.0, 0, 0, 1, 0, LW_MODIFIER_IRREDUCTIBLE);
        }
    }

    self->m_state = LW_FIGHT_STATE_RUNNING;
}


/* ---- isFinished / endTurn / startTurn ----------------------------- */

/* Java:
 *   public boolean isFinished() {
 *       if (type == TYPE_CHEST_HUNT) {
 *           for (Team team : teams) {
 *               if (team.containsChest() && team.isAlive()) return false;
 *           }
 *           return true;
 *       }
 *       int aliveTeams = 0;
 *       for (Team team : teams) {
 *           if (team.isAlive() && !team.containsChest()) {
 *               aliveTeams++;
 *               if (aliveTeams >= 2) return false;
 *           }
 *       }
 *       return true;
 *   }
 */
int lw_state_is_finished(LwState *self) {
    if (self->type == LW_FIGHT_TYPE_CHEST_HUNT) {
        for (int t = 0; t < self->n_teams; t++) {
            if (lw_team_contains_chest(self->teams[t]) && lw_team_is_alive(self->teams[t])) return 0;
        }
        return 1;
    }
    int aliveTeams = 0;
    for (int t = 0; t < self->n_teams; t++) {
        if (lw_team_is_alive(self->teams[t]) && !lw_team_contains_chest(self->teams[t])) {
            aliveTeams++;
            if (aliveTeams >= 2) return 0;
        }
    }
    return 1;
}


/* Java:
 *   public void endTurn() {
 *       if (isFinished()) {
 *           mState = State.STATE_FINISHED;
 *       } else {
 *           if (order.next()) {
 *               if (lastTurn != order.getTurn() && order.getTurn() <= State.MAX_TURNS) {
 *                   actions.log(new ActionNewTurn(order.getTurn()));
 *                   lastTurn = order.getTurn();
 *
 *                   // Battle Royale powers
 *                   if (type == State.TYPE_BATTLE_ROYALE) {
 *                       giveBRPower();
 *                   }
 *                   // Colossus: increase multiplier by 1 every 5 turns
 *                   if (type == State.TYPE_COLOSSUS && teams.size() > 1 && order.getTurn() % 5 == 1) {
 *                       colossusMultiplier++;
 *                       for (Entity e : teams.get(1).getEntities()) {
 *                           if (!e.isDead()) {
 *                               Effect.createEffect(this, Effect.TYPE_MULTIPLY_STATS, -1, 1, colossusMultiplier, 0, false, e, e, null, 0, false, 0, 1, 0, Effect.MODIFIER_IRREDUCTIBLE);
 *                           }
 *                       }
 *                   }
 *               }
 *               for (Team t : teams) t.applyCoolDown();
 *           }
 *       }
 *   }
 */
void lw_state_end_turn(LwState *self) {

    if (lw_state_is_finished(self)) {
        self->m_state = LW_FIGHT_STATE_FINISHED;
    } else {

        if (lw_order_next(&self->order)) {

            if (self->last_turn != self->order.turn && self->order.turn <= LW_FIGHT_MAX_TURNS) {
                lw_actions_log_new_turn(&self->actions, self->order.turn);
                self->last_turn = self->order.turn;

                // Battle Royale powers
                if (self->type == LW_FIGHT_TYPE_BATTLE_ROYALE) {
                    lw_state_give_br_power(self);
                }
                // Colossus: increase multiplier by 1 every 5 turns (replaces previous effect)
                if (self->type == LW_FIGHT_TYPE_COLOSSUS && self->n_teams > 1 && (self->order.turn % 5 == 1)) {
                    self->colossus_multiplier++;
                    int n;
                    struct LwEntity **es = lw_team_get_entities(self->teams[1], &n);
                    for (int i = 0; i < n; i++) {
                        struct LwEntity *e = es[i];
                        if (!lw_entity_is_dead(e)) {
                            lw_effect_create_effect(self, LW_EFFECT_TYPE_MULTIPLY_STATS, -1, 1.0,
                                                    (double)self->colossus_multiplier, 0.0, 0,
                                                    e, e, NULL, 0.0, 0, 0, 1, 0, LW_MODIFIER_IRREDUCTIBLE);
                        }
                    }
                }
            }

            for (int t = 0; t < self->n_teams; t++) {
                lw_team_apply_cooldown(self->teams[t]);
            }
        }
    }
}


/* Java:
 *   public void startTurn() throws Exception {
 *       Entity current = order.current();
 *       if (current == null) return;
 *       actions.log(new ActionEntityTurn(current));
 *       current.startTurn();
 *       if (!current.isDead()) {
 *           // TODO entity turn
 *           current.endTurn();
 *           actions.log(new ActionEndTurn(current));
 *       }
 *       endTurn();
 *   }
 */
void lw_state_start_turn(LwState *self) {

    struct LwEntity *current = lw_order_current(&self->order);
    if (current == NULL) {
        return;
    }

    lw_actions_log_entity_turn(&self->actions, current);
    // Log.i(TAG, "Start turn of " + current.getName());

    lw_entity_start_turn_proc(current);

    if (!lw_entity_is_dead(current)) {

        // TODO entity turn

        lw_entity_end_turn_proc(current);
        lw_actions_log_end_turn(&self->actions, current);
    }
    lw_state_end_turn(self);
}


/* ---- BR pulse ------------------------------------------------------ */

/* Java:
 *   public void giveBRPower() {
 *       int power = 2;
 *       for (var entity : getAllEntities(false)) {
 *           Effect.createEffect(this, Effect.TYPE_RAW_BUFF_POWER, -1, 1, power, 0, false, entity, entity, null, 0, true, 0, 1, 0, Effect.MODIFIER_IRREDUCTIBLE);
 *       }
 *   }
 */
void lw_state_give_br_power(LwState *self) {
    // X power, infinite duration
    int power = 2;
    struct LwEntity *all[LW_STATE_MAX_ENTITIES];
    int n = lw_state_get_all_entities(self, 0, all, LW_STATE_MAX_ENTITIES);
    for (int i = 0; i < n; i++) {
        struct LwEntity *entity = all[i];
        lw_effect_create_effect(self, LW_EFFECT_TYPE_RAW_BUFF_POWER, -1, 1.0,
                                (double)power, 0.0, 0,
                                entity, entity, NULL, 0.0, 1, 0, 1, 0, LW_MODIFIER_IRREDUCTIBLE);
    }
}


/* ---- computeWinner ------------------------------------------------ */

/* Java:
 *   public void computeWinner(boolean drawCheckLife) {
 *       mWinteam = -1;
 *
 *       if (type == TYPE_CHEST_HUNT) {
 *           boolean chestsAlive = false;
 *           for (var team : teams) {
 *               if (team.containsChest() && team.isAlive()) { chestsAlive = true; break; }
 *           }
 *           if (!chestsAlive) {
 *               mWinteam = -2; // Special: all alive players win
 *           }
 *           return;
 *       }
 *
 *       int alive = 0;
 *       for (int t = 0; t < teams.size(); ++t) {
 *           if (!teams.get(t).isDead() && !teams.get(t).containsChest()) {
 *               alive++;
 *               mWinteam = t;
 *           }
 *       }
 *       if (alive != 1) {
 *           mWinteam = -1;
 *       }
 *       // Égalité : on regarde la vie des équipes
 *       if (mWinteam == -1 && drawCheckLife) {
 *           if (teams.get(0).getLife() > teams.get(1).getLife()) mWinteam = 0;
 *           else if (teams.get(1).getLife() > teams.get(0).getLife()) mWinteam = 1;
 *       }
 *   }
 */
void lw_state_compute_winner(LwState *self, int drawCheckLife) {
    self->m_winteam = -1;

    if (self->type == LW_FIGHT_TYPE_CHEST_HUNT) {
        // Chest hunt (free-for-all): all alive players win if all chests are dead
        int chestsAlive = 0;
        for (int i = 0; i < self->n_teams; i++) {
            if (lw_team_contains_chest(self->teams[i]) && lw_team_is_alive(self->teams[i])) {
                chestsAlive = 1;
                break;
            }
        }
        if (!chestsAlive) {
            self->m_winteam = -2; // Special: all alive players win
        }
        return;
    }

    int alive = 0;
    for (int t = 0; t < self->n_teams; t++) {
        if (!lw_team_is_dead(self->teams[t]) && !lw_team_contains_chest(self->teams[t])) {
            alive++;
            self->m_winteam = t;
        }
    }
    if (alive != 1) {
        self->m_winteam = -1;
    }
    // Égalité : on regarde la vie des équipes
    if (self->m_winteam == -1 && drawCheckLife) {
        if (self->n_teams >= 2) {
            int l0 = lw_team_get_life(self->teams[0]);
            int l1 = lw_team_get_life(self->teams[1]);
            if (l0 > l1) {
                self->m_winteam = 0;
            } else if (l1 > l0) {
                self->m_winteam = 1;
            }
        }
    }
}


/* ---- Combat: setWeapon -------------------------------------------- */

/* Java:
 *   public boolean setWeapon(Entity entity, Weapon weapon) {
 *       // 1 TP required
 *       if (entity.getTP() <= 0) return false;
 *       entity.setWeapon(weapon);
 *       entity.useTP(1);
 *       log(new ActionSetWeapon(weapon));
 *       this.statistics.setWeapon(entity, weapon);
 *       return true;
 *   }
 */
int lw_state_set_weapon(LwState *self, struct LwEntity *entity, struct LwWeapon *weapon) {

    // 1 TP required
    if (lw_entity_get_tp(entity) <= 0) return 0;

    lw_entity_set_weapon(entity, weapon);
    lw_entity_use_tp(entity, 1);
    lw_actions_log_set_weapon(&self->actions, weapon);
    lw_stats_set_weapon(self->statistics, entity, weapon);

    return 1;
}


/* ---- useWeapon ---------------------------------------------------- */

/* Java:
 *   public int useWeapon(Entity launcher, Cell target) {
 *       if (order.current() != launcher || launcher.getWeapon() == null)
 *           return Attack.USE_INVALID_TARGET;
 *
 *       Weapon weapon = launcher.getWeapon();
 *
 *       if (weapon.getCost() > launcher.getTP())
 *           return Attack.USE_NOT_ENOUGH_TP;
 *
 *       if (weapon.getAttack().getMaxUses() != -1 && launcher.getItemUses(weapon.getId()) >= weapon.getAttack().getMaxUses())
 *           return Attack.USE_MAX_USES;
 *
 *       if (!map.canUseAttack(launcher.getCell(), target, weapon.getAttack()))
 *           return Attack.USE_INVALID_POSITION;
 *
 *       boolean critical = generateCritical(launcher);
 *       int result = critical ? Attack.USE_CRITICAL : Attack.USE_SUCCESS;
 *
 *       var cellEntity = target.getPlayer(map);
 *       ActionUseWeapon log_use = new ActionUseWeapon(target, result);
 *       actions.log(log_use);
 *       if (critical) launcher.onCritical();
 *       List<Entity> target_leeks = weapon.getAttack().applyOnCell(this, launcher, target, critical);
 *       statistics.useWeapon(launcher, weapon, target, target_leeks, cellEntity);
 *       if (critical) statistics.critical(launcher);
 *
 *       launcher.useTP(weapon.getCost());
 *       launcher.addItemUse(weapon.getId());
 *
 *       return result;
 *   }
 */
int lw_state_use_weapon(LwState *self, struct LwEntity *launcher, struct LwCell *target) {

    if (lw_order_current(&self->order) != launcher || lw_entity_get_weapon(launcher) == NULL) {
        return LW_ATTACK_USE_INVALID_TARGET;
    }

    struct LwWeapon *weapon = lw_entity_get_weapon(launcher);

    // Coût
    if (lw_weapon_get_cost(weapon) > lw_entity_get_tp(launcher)) {
        return LW_ATTACK_USE_NOT_ENOUGH_TP;
    }

    // Nombre d'utilisations par tour
    struct LwAttack *atk = lw_weapon_get_attack(weapon);
    if (lw_attack_get_max_uses(atk) != -1 &&
        lw_entity_get_item_uses(launcher, lw_weapon_get_id(weapon)) >= lw_attack_get_max_uses(atk)) {
        return LW_ATTACK_USE_MAX_USES;
    }

    // Position
    if (!lw_map_can_use_attack(self->map, lw_entity_get_cell(launcher), target, atk)) {
        return LW_ATTACK_USE_INVALID_POSITION;
    }

    int critical = lw_state_generate_critical(self, launcher);
    int result = critical ? LW_ATTACK_USE_CRITICAL : LW_ATTACK_USE_SUCCESS;

    struct LwEntity *cellEntity = lw_cell_get_player(target, self->map);
    lw_actions_log_use_weapon(&self->actions, target, result);
    if (critical) lw_entity_on_critical(launcher);
    struct LwEntity *target_leeks[LW_STATE_MAX_ENTITIES];
    int n_targets = lw_attack_apply_on_cell(atk, self, launcher, target, critical,
                                            target_leeks, LW_STATE_MAX_ENTITIES);
    lw_stats_use_weapon(self->statistics, launcher, weapon, target, target_leeks, n_targets, cellEntity);
    if (critical) lw_stats_critical(self->statistics, launcher);

    lw_entity_use_tp(launcher, lw_weapon_get_cost(weapon));
    lw_entity_add_item_use(launcher, lw_weapon_get_id(weapon));

    return result;
}


/* ---- useChip ------------------------------------------------------ */

/* Java:
 *   public int useChip(Entity caster, Cell target, Chip template) {
 *       if (order.current() != caster) return Attack.USE_INVALID_TARGET;
 *       if (template.getCost() > 0 && template.getCost() > caster.getTP())
 *           return Attack.USE_NOT_ENOUGH_TP;
 *       if (hasCooldown(caster, template))
 *           return Attack.USE_INVALID_COOLDOWN;
 *       if (template.getAttack().getMaxUses() != -1 && caster.getItemUses(template.getId()) >= template.getAttack().getMaxUses())
 *           return Attack.USE_MAX_USES;
 *       if (!target.isWalkable() || !map.canUseAttack(caster.getCell(), target, template.getAttack())) {
 *           statistics.useInvalidPosition(caster, template.getAttack(), target);
 *           return Attack.USE_INVALID_POSITION;
 *       }
 *
 *       for (EffectParameters parameters : template.getAttack().getEffects()) {
 *           if (parameters.getId() == Effect.TYPE_TELEPORT && !target.available(map))
 *               return Attack.USE_INVALID_TARGET;
 *       }
 *
 *       boolean critical = generateCritical(caster);
 *       int result = critical ? Attack.USE_CRITICAL : Attack.USE_SUCCESS;
 *
 *       var cellEntity = target.getPlayer(map);
 *       ActionUseChip log = new ActionUseChip(target, template, result);
 *       actions.log(log);
 *       if (critical) caster.onCritical();
 *       List<Entity> targets = template.getAttack().applyOnCell(this, caster, target, critical);
 *       statistics.useChip(caster, template, target, targets, cellEntity);
 *       if (critical) statistics.critical(caster);
 *
 *       if (template.getCooldown() != 0) addCooldown(caster, template);
 *
 *       caster.useTP(template.getCost());
 *       caster.addItemUse(template.getId());
 *
 *       return result;
 *   }
 */
int lw_state_use_chip(LwState *self, struct LwEntity *caster, struct LwCell *target, struct LwChip *template_) {

    if (lw_order_current(&self->order) != caster) {
        return LW_ATTACK_USE_INVALID_TARGET;
    }
    if (lw_chip_get_cost(template_) > 0 && lw_chip_get_cost(template_) > lw_entity_get_tp(caster)) {
        return LW_ATTACK_USE_NOT_ENOUGH_TP;
    }
    if (lw_state_has_cooldown(self, caster, template_)) {
        return LW_ATTACK_USE_INVALID_COOLDOWN;
    }
    // Nombre d'utilisations par tour
    struct LwAttack *atk = lw_chip_get_attack(template_);
    if (lw_attack_get_max_uses(atk) != -1 &&
        lw_entity_get_item_uses(caster, lw_chip_get_id(template_)) >= lw_attack_get_max_uses(atk)) {
        return LW_ATTACK_USE_MAX_USES;
    }
    if (!lw_cell_is_walkable(target) || !lw_map_can_use_attack(self->map, lw_entity_get_cell(caster), target, atk)) {
        lw_stats_use_invalid_position(self->statistics, caster, atk, target);
        return LW_ATTACK_USE_INVALID_POSITION;
    }
    // Invocation mais sans IA
    // TODO

    int n_eff = lw_attack_get_effects_count(atk);
    for (int i = 0; i < n_eff; i++) {
        struct LwEffectParameters *parameters = lw_attack_get_effect_params_at(atk, i);
        // Si c'est une téléportation on ajoute une petite vérification
        if (lw_effect_params_get_id(parameters) == LW_EFFECT_TYPE_TELEPORT && !lw_cell_available(target, self->map)) {
            return LW_ATTACK_USE_INVALID_TARGET;
        }
    }

    int critical = lw_state_generate_critical(self, caster);
    int result = critical ? LW_ATTACK_USE_CRITICAL : LW_ATTACK_USE_SUCCESS;

    struct LwEntity *cellEntity = lw_cell_get_player(target, self->map);
    lw_actions_log_use_chip(&self->actions, target, template_, result);
    if (critical) lw_entity_on_critical(caster);
    struct LwEntity *targets[LW_STATE_MAX_ENTITIES];
    int n_targets = lw_attack_apply_on_cell(atk, self, caster, target, critical,
                                            targets, LW_STATE_MAX_ENTITIES);
    lw_stats_use_chip(self->statistics, caster, template_, target, targets, n_targets, cellEntity);
    if (critical) lw_stats_critical(self->statistics, caster);

    if (lw_chip_get_cooldown(template_) != 0) {
        lw_state_add_cooldown_default(self, caster, template_);
    }

    lw_entity_use_tp(caster, lw_chip_get_cost(template_));
    lw_entity_add_item_use(caster, lw_chip_get_id(template_));

    return result;
}


/* ---- Movement ----------------------------------------------------- */

/* Java:
 *   public int moveEntity(Entity entity, List<Cell> path) {
 *       if (entity.hasState(EntityState.STATIC)) return 0;
 *       int size = path.size();
 *       if (size == 0) return 0;
 *       if (size > entity.getMP()) return 0;
 *       actions.log(new ActionMove(entity, path));
 *       statistics.move(entity, entity, entity.getCell(), path);
 *       entity.useMP(size);
 *       this.map.moveEntity(entity, path.get(path.size() - 1));
 *       return path.size();
 *   }
 */
int lw_state_move_entity_path(LwState *self, struct LwEntity *entity,
                              struct LwCell *const *path, int path_len) {

    if (lw_entity_has_state(entity, LW_ENTITY_STATE_STATIC)) return 0; // Static entity cannot move.

    int size = path_len;
    if (size == 0) {
        return 0;
    }
    if (size > lw_entity_get_mp(entity)) {
        return 0;
    }

    lw_actions_log_move(&self->actions, entity, (const struct LwCell *const *)path, path_len);
    lw_stats_move(self->statistics, entity, entity, lw_entity_get_cell(entity),
                  (struct LwCell **)path, path_len);

    lw_entity_use_mp(entity, size);
    lw_map_move_entity(self->map, entity, (struct LwCell*)path[path_len - 1]);

    return path_len;
}


/* Java:
 *   public void moveEntity(Entity entity, Cell cell) {
 *       if (entity.hasState(EntityState.STATIC)) return;
 *       this.map.moveEntity(entity, cell);
 *   }
 */
void lw_state_move_entity_cell(LwState *self, struct LwEntity *entity, struct LwCell *cell) {

    if (lw_entity_has_state(entity, LW_ENTITY_STATE_STATIC)) return; // Static entity cannot move.

    lw_map_move_entity(self->map, entity, cell);
}


/* Java:
 *   public void teleportEntity(Entity entity, Cell cell, Entity caster, int itemId) {
 *       Cell start = entity.getCell();
 *       this.map.moveEntity(entity, cell);
 *       statistics.move(caster, entity, start, new ArrayList<>(Arrays.asList(cell)));
 *       if (start != cell) entity.onMoved(caster);
 *       statistics.teleportation(entity, caster, start, cell, itemId);
 *   }
 */
void lw_state_teleport_entity(LwState *self, struct LwEntity *entity, struct LwCell *cell,
                              struct LwEntity *caster, int itemId) {

    struct LwCell *start = lw_entity_get_cell(entity);
    lw_map_move_entity(self->map, entity, cell);

    struct LwCell *path1[1]; path1[0] = cell;
    lw_stats_move(self->statistics, caster, entity, start, path1, 1);

    if (start != cell) {
        lw_entity_on_moved(entity, caster);
    }

    lw_stats_teleportation(self->statistics, entity, caster, start, cell, itemId);
}


/* Java:
 *   public void slideEntity(Entity entity, Cell cell, Entity caster) {
 *       if (entity.hasState(EntityState.STATIC)) return;
 *       Cell start = entity.getCell();
 *       if (cell != start) {
 *           this.map.moveEntity(entity, cell);
 *           statistics.move(caster, entity, start, map.getAStarPath(start, new Cell[] { cell }, Arrays.asList(cell, start)));
 *           statistics.slide(entity, caster, start, cell);
 *           entity.onMoved(caster);
 *       }
 *   }
 */
void lw_state_slide_entity(LwState *self, struct LwEntity *entity, struct LwCell *cell,
                           struct LwEntity *caster) {

    if (lw_entity_has_state(entity, LW_ENTITY_STATE_STATIC)) return;

    struct LwCell *start = lw_entity_get_cell(entity);

    if (cell != start) {
        lw_map_move_entity(self->map, entity, cell);

        struct LwCell *targets[1]; targets[0] = cell;
        struct LwCell *forbidden[2]; forbidden[0] = cell; forbidden[1] = start;
        struct LwCell *path[64];
        int n_path = lw_map_get_a_star_path(self->map, start, targets, 1,
                                            forbidden, 2, path, 64);
        lw_stats_move(self->statistics, caster, entity, start, path, n_path);
        lw_stats_slide(self->statistics, entity, caster, start, cell);
        lw_entity_on_moved(entity, caster);
    }
}


/* Java:
 *   public void invertEntities(Entity caster, Entity target) {
 *       if (target.hasState(EntityState.STATIC)) return;
 *       Cell start = caster.getCell();
 *       Cell end = target.getCell();
 *       if (start == null || end == null) return;
 *       this.map.invertEntities(caster, target);
 *       statistics.move(caster, caster, start, new ArrayList<>(Arrays.asList(end)));
 *       statistics.move(caster, target, end, new ArrayList<>(Arrays.asList(start)));
 *       target.onMoved(caster);
 *       caster.onMoved(caster);
 *   }
 */
void lw_state_invert_entities(LwState *self, struct LwEntity *caster, struct LwEntity *target) {

    if (lw_entity_has_state(target, LW_ENTITY_STATE_STATIC)) return;

    struct LwCell *start = lw_entity_get_cell(caster);
    struct LwCell *end   = lw_entity_get_cell(target);
    if (start == NULL || end == NULL) {
        return;
    }

    lw_map_invert_entities(self->map, caster, target);

    struct LwCell *p1[1]; p1[0] = end;
    struct LwCell *p2[1]; p2[0] = start;
    lw_stats_move(self->statistics, caster, caster, start, p1, 1);
    lw_stats_move(self->statistics, caster, target, end,   p2, 1);

    // Passifs
    lw_entity_on_moved(target, caster);
    lw_entity_on_moved(caster, caster);
}


/* ---- Summons ------------------------------------------------------ */

/* Java: public int summonEntity(Entity caster, Cell target, Chip template) {
 *           return summonEntity(caster, target, template, null);
 *       } */
int lw_state_summon_entity(LwState *self, struct LwEntity *caster, struct LwCell *target,
                            struct LwChip *template_) {
    return lw_state_summon_entity_named(self, caster, target, template_, NULL);
}


/* Java:
 *   public int summonEntity(Entity caster, Cell target, Chip template, String name) {
 *       EffectParameters params = template.getAttack().getEffectParametersByType(Effect.TYPE_SUMMON);
 *       if (order.current() != caster || params == null) return -1;
 *       if (template.getCost() > caster.getTP()) return -2;
 *       if (hasCooldown(caster, template)) return -3;
 *       if (!map.canUseAttack(caster.getCell(), target, template.getAttack())) return -4;
 *       if (!target.available(map)) return -4;
 *       if (teams.get(caster.getTeam()).getSummonCount() >= SUMMON_LIMIT) return -5;
 *
 *       boolean critical = generateCritical(caster);
 *       int result = critical ? Attack.USE_CRITICAL : Attack.USE_SUCCESS;
 *
 *       ActionUseChip log = new ActionUseChip(target, template, result);
 *       actions.log(log);
 *       if (critical) caster.onCritical();
 *
 *       Entity summon = createSummon(caster, (int) params.getValue1(), target, template.getLevel(), critical, name);
 *
 *       actions.log(new ActionInvocation(summon, result));
 *       statistics.summon(caster, summon);
 *       statistics.useChip(caster, template, target, new ArrayList<>(), null);
 *
 *       if (template.getCooldown() != 0) addCooldown(caster, template);
 *
 *       caster.useTP(template.getCost());
 *
 *       return result;
 *   }
 */
int lw_state_summon_entity_named(LwState *self, struct LwEntity *caster, struct LwCell *target,
                                  struct LwChip *template_, const char *name) {

    struct LwAttack *atk = lw_chip_get_attack(template_);
    struct LwEffectParameters *params = lw_attack_get_effect_params_by_type(atk, LW_EFFECT_TYPE_SUMMON);
    if (lw_order_current(&self->order) != caster || params == NULL) return -1;
    if (lw_chip_get_cost(template_) > lw_entity_get_tp(caster)) return -2;
    if (lw_state_has_cooldown(self, caster, template_)) return -3;
    if (!lw_map_can_use_attack(self->map, lw_entity_get_cell(caster), target, atk)) return -4;
    if (!lw_cell_available(target, self->map)) return -4;
    if (lw_team_get_summon_count(self->teams[lw_entity_get_team(caster)]) >= LW_SUMMON_LIMIT) return -5;

    int critical = lw_state_generate_critical(self, caster);
    int result = critical ? LW_ATTACK_USE_CRITICAL : LW_ATTACK_USE_SUCCESS;

    lw_actions_log_use_chip(&self->actions, target, template_, result);
    if (critical) lw_entity_on_critical(caster);

    // On invoque
    struct LwEntity *summon = lw_state_create_summon(self, caster, (int)lw_effect_params_get_value1(params),
                                                     target, lw_chip_get_level(template_), critical, name);

    // On balance l'action
    lw_actions_log_invocation(&self->actions, summon, result);
    lw_stats_summon(self->statistics, caster, summon);
    lw_stats_use_chip(self->statistics, caster, template_, target, NULL, 0, NULL);

    if (lw_chip_get_cooldown(template_) != 0) {
        lw_state_add_cooldown_default(self, caster, template_);
    }

    lw_entity_use_tp(caster, lw_chip_get_cost(template_));

    return result;
}


/* Java:
 *   public Bulb createSummon(Entity owner, int type, Cell target, int level, boolean critical, String name) {
 *       int fid = getNextEntityId();
 *       Bulb invoc = Bulb.create(owner, -fid, type, level, critical, name);
 *       invoc.setState(this, fid);
 *       int team = owner.getTeam();
 *       invoc.setTeam(team);
 *       teams.get(team).addEntity(invoc);
 *       mEntities.put(invoc.getFId(), invoc);
 *       order.addSummon(owner, invoc);
 *       this.map.setEntity(invoc, target);
 *       actions.addEntity(invoc, critical);
 *       return invoc;
 *   }
 */
struct LwEntity* lw_state_create_summon(LwState *self, struct LwEntity *owner, int type,
                                        struct LwCell *target, int level, int critical,
                                        const char *name) {

    int fid = lw_state_get_next_entity_id(self);
    struct LwEntity *invoc = lw_bulb_create(owner, -fid, type, level, critical, name);
    lw_entity_set_state(invoc, self, fid);

    int team = lw_entity_get_team(owner);

    lw_entity_set_team(invoc, team);
    lw_team_add_entity(self->teams[team], invoc);

    // On ajoute dans les tableaux
    if (self->n_entities < LW_STATE_MAX_ENTITIES) {
        self->m_entities[self->n_entities++] = invoc;
    }

    // On ajoute dans l'ordre de jeu
    lw_order_add_summon(&self->order, owner, invoc);

    // On met la cellule
    lw_map_set_entity(self->map, invoc, target);

    // On l'ajoute dans les infos du combat
    lw_actions_add_entity(&self->actions, invoc, critical);

    return invoc;
}


/* Java:
 *   public void removeInvocation(Entity invoc, boolean force) {
 *       teams.get(invoc.getTeam()).removeEntity(invoc);
 *       if (force) mEntities.remove(invoc.getFId());
 *   }
 */
void lw_state_remove_invocation(LwState *self, struct LwEntity *invoc, int force) {

    // Mort d'une invocation, on la retire des listes
    lw_team_remove_entity(self->teams[lw_entity_get_team(invoc)], invoc);

    if (force) {
        int fid = lw_entity_get_fid(invoc);
        for (int i = 0; i < self->n_entities; i++) {
            if (lw_entity_get_fid(self->m_entities[i]) == fid) {
                for (int j = i; j + 1 < self->n_entities; j++) {
                    self->m_entities[j] = self->m_entities[j + 1];
                }
                self->n_entities--;
                break;
            }
        }
    }
}


/* ---- Resurrect --------------------------------------------------- */

/* Java:
 *   public int resurrectEntity(Entity caster, Cell target, Chip template, Entity target_entity, boolean fullLife) {
 *       if (order.current() != caster) return Attack.USE_INVALID_TARGET;
 *       if (template.getCost() > caster.getTP()) return Attack.USE_NOT_ENOUGH_TP;
 *       if (!map.canUseAttack(caster.getCell(), target, template.getAttack())) return Attack.USE_INVALID_POSITION;
 *       if (hasCooldown(caster, template)) return Attack.USE_INVALID_COOLDOWN;
 *       EffectParameters params = template.getAttack().getEffectParametersByType(Effect.TYPE_RESURRECT);
 *       if (params == null || !target.available(map) || !target_entity.isDead()) return Attack.USE_INVALID_TARGET;
 *
 *       if (target_entity.isSummon()) {
 *           if (teams.get(target_entity.getTeam()).getSummonCount() >= SUMMON_LIMIT)
 *               return Attack.USE_TOO_MANY_SUMMONS;
 *       }
 *
 *       boolean critical = generateCritical(caster);
 *       int result = critical ? Attack.USE_CRITICAL : Attack.USE_SUCCESS;
 *
 *       ActionUseChip log = new ActionUseChip(target, template, result);
 *       actions.log(log);
 *       if (critical) caster.onCritical();
 *
 *       resurrect(caster, target_entity, target, critical, fullLife);
 *       statistics.useChip(caster, template, target, new ArrayList<>(), null);
 *       statistics.resurrect(caster, target_entity);
 *
 *       if (template.getCooldown() != 0) addCooldown(caster, template);
 *
 *       if (result > 0 && template.getId() == 415) {
 *           Effect.createEffect(this, Effect.TYPE_ADD_STATE, -1, 1.0, 3.0, 3.0, critical, target_entity, caster, template.getAttack(), 1.0, true, 0, 1, 0, Effect.MODIFIER_IRREDUCTIBLE);
 *       }
 *
 *       caster.useTP(template.getCost());
 *
 *       return result;
 *   }
 */
int lw_state_resurrect_entity(LwState *self, struct LwEntity *caster, struct LwCell *target,
                              struct LwChip *template_, struct LwEntity *target_entity, int fullLife) {

    if (lw_order_current(&self->order) != caster) {
        return LW_ATTACK_USE_INVALID_TARGET;
    }
    if (lw_chip_get_cost(template_) > lw_entity_get_tp(caster)) {
        return LW_ATTACK_USE_NOT_ENOUGH_TP;
    }
    struct LwAttack *atk = lw_chip_get_attack(template_);
    if (!lw_map_can_use_attack(self->map, lw_entity_get_cell(caster), target, atk)) {
        return LW_ATTACK_USE_INVALID_POSITION;
    }
    if (lw_state_has_cooldown(self, caster, template_)) {
        return LW_ATTACK_USE_INVALID_COOLDOWN;
    }
    struct LwEffectParameters *params = lw_attack_get_effect_params_by_type(atk, LW_EFFECT_TYPE_RESURRECT);
    if (params == NULL || !lw_cell_available(target, self->map) || !lw_entity_is_dead(target_entity)) {
        return LW_ATTACK_USE_INVALID_TARGET;
    }

    if (lw_entity_is_summon(target_entity)) {
        // It's a summon
        if (lw_team_get_summon_count(self->teams[lw_entity_get_team(target_entity)]) >= LW_SUMMON_LIMIT) {
            return LW_ATTACK_USE_TOO_MANY_SUMMONS;
        }
    }

    int critical = lw_state_generate_critical(self, caster);
    int result = critical ? LW_ATTACK_USE_CRITICAL : LW_ATTACK_USE_SUCCESS;

    lw_actions_log_use_chip(&self->actions, target, template_, result);
    if (critical) lw_entity_on_critical(caster);

    // Resurrect
    lw_state_resurrect(self, caster, target_entity, target, critical, fullLife);
    lw_stats_use_chip(self->statistics, caster, template_, target, NULL, 0, NULL);
    lw_stats_resurrect(self->statistics, caster, target_entity);

    if (lw_chip_get_cooldown(template_) != 0) {
        lw_state_add_cooldown_default(self, caster, template_);
    }

    // Hardcode awekening invulnerability
    if (result > 0 && lw_chip_get_id(template_) == 415) {
        lw_effect_create_effect(self, LW_EFFECT_TYPE_ADD_STATE, -1, 1.0, 3.0, 3.0, critical,
                                target_entity, caster, atk, 1.0, 1, 0, 1, 0, LW_MODIFIER_IRREDUCTIBLE);
    }

    lw_entity_use_tp(caster, lw_chip_get_cost(template_));

    return result;
}


/* Java:
 *   public void resurrect(Entity owner, Entity entity, Cell cell, boolean critical, boolean fullLife) {
 *       Entity next = null;
 *       boolean start = false;
 *       for (Entity e : initialOrder) {
 *           if (e == entity) { start = true; continue; }
 *           if (!start) continue;
 *           if (e.isDead()) continue;
 *           next = e;
 *           break;
 *       }
 *       if (next == null) order.addEntity(entity);
 *       else              order.addEntity(order.getEntityTurnOrder(next) - 1, entity);
 *       entity.resurrect(owner, critical ? Effect.CRITICAL_FACTOR : 1.0, fullLife);
 *       this.map.setEntity(entity, cell);
 *       actions.log(new ActionResurrect(owner, entity));
 *   }
 */
void lw_state_resurrect(LwState *self, struct LwEntity *owner, struct LwEntity *entity,
                        struct LwCell *cell, int critical, int fullLife) {

    struct LwEntity *next = NULL;
    int start = 0;
    for (int i = 0; i < self->n_initial_order; i++) {
        struct LwEntity *e = self->initial_order[i];
        if (e == entity) {
            start = 1;
            continue;
        }
        if (!start) {
            continue;
        }
        if (lw_entity_is_dead(e)) {
            continue;
        }
        next = e;
        break;
    }
    if (next == NULL) {
        lw_order_add_entity(&self->order, entity);
    } else {
        lw_order_add_entity_at(&self->order, lw_order_get_entity_turn_order(&self->order, next) - 1, entity);
    }
    lw_entity_resurrect_proc(entity, owner, critical ? LW_CRITICAL_FACTOR_F : 1.0, fullLife);

    // On met la cellule
    lw_map_set_entity(self->map, entity, cell);

    // On balance l'action
    lw_actions_log_resurrect(&self->actions, owner, entity);
}


/* ---- onPlayerDie ---------------------------------------------------- */

/* Java:
 *   public void onPlayerDie(Entity entity, Entity killer, Item item) {
 *       var killCell = entity.getCell();
 *       order.removeEntity(entity);
 *       this.map.removeEntity(entity);
 *       actions.log(new ActionEntityDie(entity, killer));
 *       statistics.kill(killer, entity, item, killCell);
 *
 *       // BR : give 10 (or 2 for bulb) power + 50% of power to the killer
 *       if (this.type == State.TYPE_BATTLE_ROYALE && killer != null) {
 *           int amount = entity.isSummon() ? 2 : 10;
 *           var effect = entity.getEffects().stream().filter(e -> e.getAttack() == null && e.getID() == Effect.TYPE_RAW_BUFF_POWER).findAny().orElse(null);
 *           if (effect != null) amount += (int) (effect.value / 2);
 *           Effect.createEffect(this, Effect.TYPE_RAW_BUFF_POWER, -1, 1, amount, 0, false, killer, killer, null, 0, true, 0, 1, 0, Effect.MODIFIER_IRREDUCTIBLE);
 *       }
 *
 *       // Coffre ouvert
 *       if (entity.getType() == Entity.TYPE_CHEST && entity.getResurrected() == 0) {
 *           if (this.context != State.CONTEXT_CHALLENGE) {
 *               var resources = entity.loot(this);
 *               actions.log(new ActionChestOpened(killer, entity, resources));
 *               statistics.chestKilled(killer, entity, resources);
 *           }
 *           int amount = entity.getLevel() == 100 ? 10 : (entity.getLevel() == 200 ? 50 : 100);
 *           Effect.createEffect(this, Effect.TYPE_RAW_BUFF_POWER, -1, 1, amount, 0, false, killer, killer, null, 0, true, 0, 1, 0, Effect.MODIFIER_IRREDUCTIBLE);
 *       }
 *
 *       if (!entity.isSummon()) {
 *           for (var ally : getTeamEntities(entity.getTeam())) {
 *               if (ally == entity) continue;
 *               ally.onAllyKilled();
 *           }
 *       }
 *
 *       if (killer != null) killer.onKill();
 *   }
 */
void lw_state_on_player_die(LwState *self, struct LwEntity *entity,
                            struct LwEntity *killer, struct LwItem *item) {

    struct LwCell *killCell = lw_entity_get_cell(entity);

    lw_order_remove_entity(&self->order, entity);
    lw_map_remove_entity(self->map, entity);

    lw_actions_log_entity_die(&self->actions, entity, killer);
    lw_stats_kill(self->statistics, killer, entity, item, killCell);

    // BR : give 10 (or 2 for bulb) power + 50% of power to the killer
    if (self->type == LW_FIGHT_TYPE_BATTLE_ROYALE && killer != NULL) {
        int amount = lw_entity_is_summon(entity) ? 2 : 10;
        /* Java's stream.filter: pick first effect with attack==null && id==TYPE_RAW_BUFF_POWER.
         * lw_entity_get_effect_buffpower_value returns (int)(value / 2) for that effect, or 0. */
        amount += lw_entity_get_effect_buffpower_value(entity);
        lw_effect_create_effect(self, LW_EFFECT_TYPE_RAW_BUFF_POWER, -1, 1.0,
                                (double)amount, 0.0, 0,
                                killer, killer, NULL, 0.0, 1, 0, 1, 0, LW_MODIFIER_IRREDUCTIBLE);
    }

    // Coffre ouvert
    if (lw_entity_get_type(entity) == LW_ENTITY_TYPE_CHEST && lw_entity_get_resurrected(entity) == 0) {

        if (self->context != LW_CONTEXT_CHALLENGE) {
            /* Java: var resources = entity.loot(this); ... statistics.chestKilled(killer, entity, resources)
             * Resources is a Map<int,int> -- the C action stream stub drops it; we still call the hook. */
            (void)lw_entity_loot(entity, self);
            lw_actions_log_chest_opened(&self->actions, killer, entity);
            lw_stats_chest_killed(self->statistics, killer, entity);
        }

        int level = lw_entity_get_level(entity);
        int amount = level == 100 ? 10 : (level == 200 ? 50 : 100);
        lw_effect_create_effect(self, LW_EFFECT_TYPE_RAW_BUFF_POWER, -1, 1.0,
                                (double)amount, 0.0, 0,
                                killer, killer, NULL, 0.0, 1, 0, 1, 0, LW_MODIFIER_IRREDUCTIBLE);
    }

    // Passive effect ally killed
    if (!lw_entity_is_summon(entity)) {
        struct LwEntity *allies[LW_STATE_MAX_ENTITIES];
        int n = lw_state_get_team_entities(self, lw_entity_get_team(entity), allies, LW_STATE_MAX_ENTITIES);
        for (int i = 0; i < n; i++) {
            struct LwEntity *ally = allies[i];
            if (ally == entity) continue;
            lw_entity_on_ally_killed(ally);
        }
    }

    // Passive effect kill
    if (killer != NULL) {
        lw_entity_on_kill(killer);
    }
}


/* ---- moveToward / moveTowardCell ---------------------------------- */

/* Java:
 *   public long moveToward(Entity entity, long leek_id, long pm_to_use) {
 *       int pm = pm_to_use == -1 ? entity.getMP() : (int) pm_to_use;
 *       if (pm > entity.getMP()) pm = entity.getMP();
 *       long used_pm = 0;
 *       if (pm > 0) {
 *           var target = getEntity(leek_id);
 *           if (target != null && !target.isDead()) {
 *               var path = getMap().getPathBeetween(entity.getCell(), target.getCell(), null);
 *               if (path != null) used_pm = moveEntity(entity, path.subList(0, Math.min(path.size(), pm)));
 *           }
 *       }
 *       return used_pm;
 *   }
 */
int64_t lw_state_move_toward(LwState *self, struct LwEntity *entity, int64_t leek_id, int64_t pm_to_use) {

    int pm = pm_to_use == -1 ? lw_entity_get_mp(entity) : (int)pm_to_use;
    if (pm > lw_entity_get_mp(entity)) {
        pm = lw_entity_get_mp(entity);
    }
    int64_t used_pm = 0;
    if (pm > 0) {
        struct LwEntity *target = lw_state_get_entity(self, (int)leek_id);
        if (target != NULL && !lw_entity_is_dead(target)) {
            struct LwCell *path[64];
            int n_path = lw_map_get_path_between(self->map, lw_entity_get_cell(entity),
                                                 lw_entity_get_cell(target), path, 64);
            if (n_path >= 0) {
                int n_take = n_path < pm ? n_path : pm;
                used_pm = lw_state_move_entity_path(self, entity, path, n_take);
            }
        }
    }
    return used_pm;
}


/* Java:
 *   public long moveTowardCell(Entity entity, long cell_id, long pm_to_use) {
 *       int pm = pm_to_use == -1 ? entity.getMP() : (int) pm_to_use;
 *       if (pm > entity.getMP()) pm = entity.getMP();
 *       long used_pm = 0;
 *       if (pm > 0 && entity.getCell() != null) {
 *           Cell target = map.getCell((int) cell_id);
 *           if (target != null && target != entity.getCell()) {
 *               List<Cell> path = null;
 *               if (!target.isWalkable())
 *                   path = map.getAStarPath(entity.getCell(), map.getValidCellsAroundObstacle(target), null);
 *               else
 *                   path = getMap().getPathBeetween(entity.getCell(), target, null);
 *               if (path != null) used_pm = moveEntity(entity, path.subList(0, Math.min(pm, path.size())));
 *           }
 *       }
 *       return used_pm;
 *   }
 */
int64_t lw_state_move_toward_cell(LwState *self, struct LwEntity *entity, int64_t cell_id, int64_t pm_to_use) {

    int pm = pm_to_use == -1 ? lw_entity_get_mp(entity) : (int)pm_to_use;
    if (pm > lw_entity_get_mp(entity)) {
        pm = lw_entity_get_mp(entity);
    }
    int64_t used_pm = 0;
    if (pm > 0 && lw_entity_get_cell(entity) != NULL) {
        struct LwCell *target = lw_map_get_cell(self->map, (int)cell_id);
        if (target != NULL && target != lw_entity_get_cell(entity)) {
            struct LwCell *path[64];
            int n_path = -1;
            if (!lw_cell_is_walkable(target)) {
                struct LwCell *valid[16];
                int n_valid = lw_map_get_valid_cells_around_obstacle(self->map, target, valid, 16);
                n_path = lw_map_get_a_star_path(self->map, lw_entity_get_cell(entity),
                                                valid, n_valid, NULL, 0, path, 64);
            } else {
                n_path = lw_map_get_path_between(self->map, lw_entity_get_cell(entity), target, path, 64);
            }
            if (n_path >= 0) {
                int n_take = pm < n_path ? pm : n_path;
                used_pm = lw_state_move_entity_path(self, entity, path, n_take);
            }
        }
    }
    return used_pm;
}


/* ---- Static helpers -------------------------------------------------- */

/* Java type-id constants used in the dispatch (mirror State.java). */
#define LW_FT_TYPE_SOLO_GARDEN          1
#define LW_FT_TYPE_SOLO_TEST            2
#define LW_FT_TYPE_TEAM_GARDEN          4
#define LW_FT_TYPE_SOLO_CHALLENGE       5
#define LW_FT_TYPE_FARMER_GARDEN        6
#define LW_FT_TYPE_SOLO_TOURNAMENT      7
#define LW_FT_TYPE_TEAM_TEST            8
#define LW_FT_TYPE_FARMER_TOURNAMENT    9
#define LW_FT_TYPE_TEAM_TOURNAMENT     10
#define LW_FT_TYPE_FARMER_CHALLENGE    11
#define LW_FT_TYPE_FARMER_TEST         12
#define LW_FT_FULL_BATTLE_ROYALE       15
#define LW_FT_FULL_WAR_GARDEN          20
#define LW_FT_FULL_WAR_AUTO            21
#define LW_FT_FULL_CHEST_HUNT_GARDEN   22
#define LW_FT_FULL_CHEST_HUNT_AUTO     23
#define LW_FT_FULL_COLOSSUS_GARDEN     24
#define LW_FT_FULL_COLOSSUS_AUTO       25


/* Java:
 *   public static int getFightContext(int type) {
 *       if (type == TYPE_SOLO_GARDEN || type == TYPE_TEAM_GARDEN || type == TYPE_FARMER_GARDEN || type == FULL_TYPE_WAR_GARDEN || type == FULL_TYPE_CHEST_HUNT_GARDEN || type == FULL_TYPE_COLOSSUS_GARDEN) return CONTEXT_GARDEN;
 *       else if (type == TYPE_SOLO_TEST || type == TYPE_TEAM_TEST || type == TYPE_FARMER_TEST) return CONTEXT_TEST;
 *       else if (type == TYPE_TEAM_TOURNAMENT || type == TYPE_SOLO_TOURNAMENT || type == TYPE_FARMER_TOURNAMENT || type == FULL_TYPE_WAR_AUTO || type == FULL_TYPE_CHEST_HUNT_AUTO || type == FULL_TYPE_COLOSSUS_AUTO) return CONTEXT_TOURNAMENT;
 *       else if (type == FULL_TYPE_BATTLE_ROYALE) return CONTEXT_BATTLE_ROYALE;
 *       return CONTEXT_CHALLENGE;
 *   }
 */
int lw_state_get_fight_context_static(int type) {
    if (type == LW_FT_TYPE_SOLO_GARDEN || type == LW_FT_TYPE_TEAM_GARDEN || type == LW_FT_TYPE_FARMER_GARDEN
        || type == LW_FT_FULL_WAR_GARDEN || type == LW_FT_FULL_CHEST_HUNT_GARDEN || type == LW_FT_FULL_COLOSSUS_GARDEN) {
        return LW_CONTEXT_GARDEN;
    } else if (type == LW_FT_TYPE_SOLO_TEST || type == LW_FT_TYPE_TEAM_TEST || type == LW_FT_TYPE_FARMER_TEST) {
        return LW_CONTEXT_TEST;
    } else if (type == LW_FT_TYPE_TEAM_TOURNAMENT || type == LW_FT_TYPE_SOLO_TOURNAMENT || type == LW_FT_TYPE_FARMER_TOURNAMENT
               || type == LW_FT_FULL_WAR_AUTO || type == LW_FT_FULL_CHEST_HUNT_AUTO || type == LW_FT_FULL_COLOSSUS_AUTO) {
        return LW_CONTEXT_TOURNAMENT;
    } else if (type == LW_FT_FULL_BATTLE_ROYALE) {
        return LW_CONTEXT_BATTLE_ROYALE;
    }
    return LW_CONTEXT_CHALLENGE;
}


/* Java:
 *   public static int getFightType(int type) {
 *       if (type == TYPE_SOLO_GARDEN || type == TYPE_SOLO_CHALLENGE || type == TYPE_SOLO_TOURNAMENT || type == TYPE_SOLO_TEST) return State.TYPE_SOLO;
 *       else if (type == TYPE_FARMER_GARDEN || type == TYPE_FARMER_TOURNAMENT || type == TYPE_FARMER_CHALLENGE || type == TYPE_FARMER_TEST) return State.TYPE_FARMER;
 *       else if (type == FULL_TYPE_BATTLE_ROYALE) return State.TYPE_BATTLE_ROYALE;
 *       else if (type == FULL_TYPE_WAR_GARDEN || type == FULL_TYPE_WAR_AUTO) return State.TYPE_WAR;
 *       else if (type == FULL_TYPE_CHEST_HUNT_GARDEN || type == FULL_TYPE_CHEST_HUNT_AUTO) return State.TYPE_CHEST_HUNT;
 *       else if (type == FULL_TYPE_COLOSSUS_GARDEN || type == FULL_TYPE_COLOSSUS_AUTO) return State.TYPE_COLOSSUS;
 *       return State.TYPE_TEAM;
 *   }
 */
int lw_state_get_fight_type_static(int type) {
    if (type == LW_FT_TYPE_SOLO_GARDEN || type == LW_FT_TYPE_SOLO_CHALLENGE
        || type == LW_FT_TYPE_SOLO_TOURNAMENT || type == LW_FT_TYPE_SOLO_TEST) {
        return LW_FIGHT_TYPE_SOLO;
    } else if (type == LW_FT_TYPE_FARMER_GARDEN || type == LW_FT_TYPE_FARMER_TOURNAMENT
               || type == LW_FT_TYPE_FARMER_CHALLENGE || type == LW_FT_TYPE_FARMER_TEST) {
        return LW_FIGHT_TYPE_FARMER;
    } else if (type == LW_FT_FULL_BATTLE_ROYALE) {
        return LW_FIGHT_TYPE_BATTLE_ROYALE;
    } else if (type == LW_FT_FULL_WAR_GARDEN || type == LW_FT_FULL_WAR_AUTO) {
        return LW_FIGHT_TYPE_WAR;
    } else if (type == LW_FT_FULL_CHEST_HUNT_GARDEN || type == LW_FT_FULL_CHEST_HUNT_AUTO) {
        return LW_FIGHT_TYPE_CHEST_HUNT;
    } else if (type == LW_FT_FULL_COLOSSUS_GARDEN || type == LW_FT_FULL_COLOSSUS_AUTO) {
        return LW_FIGHT_TYPE_COLOSSUS;
    }
    return LW_FIGHT_TYPE_TEAM;
}


/* Java: public static boolean isTeamAIFight(int type) { return getFightType(type) != TYPE_SOLO; } */
int lw_state_is_team_ai_fight(int type) {
    return lw_state_get_fight_type_static(type) != LW_FIGHT_TYPE_SOLO;
}

/* Java: public static boolean isTestFight(int type) { return getFightContext(type) == CONTEXT_TEST; } */
int lw_state_is_test_fight(int type) {
    return lw_state_get_fight_context_static(type) == LW_CONTEXT_TEST;
}

/* Java: public static boolean isChallenge(int type) { return getFightContext(type) == CONTEXT_CHALLENGE; } */
int lw_state_is_challenge(int type) {
    return lw_state_get_fight_context_static(type) == LW_CONTEXT_CHALLENGE;
}


/* ---- Progression --------------------------------------------------- */

/* Java:
 *   private double getFactionProgress() {
 *       int teamCount = teams.size();
 *       if (teamCount <= 1) return 0;
 *       double progress = 0;
 *       for (Team team : teams) progress += team.getLifeRatio();
 *       return 1 - progress / teamCount;
 *   }
 */
static double get_faction_progress(LwState *self) {
    int teamCount = self->n_teams;
    if (teamCount <= 1) return 0;

    double progress = 0;
    for (int i = 0; i < teamCount; i++) {
        progress += lw_team_get_life_ratio(self->teams[i]);
    }
    return 1.0 - progress / teamCount;
}


/* Java:
 *   public double getProgress() {
 *       if (this.order == null) return 0;
 *       int entityCount = this.order.getEntities().size();
 *       double f = getFactionProgress();
 *       double t = Math.min(1, Math.pow((double)(this.getTurn() * entityCount + this.order.getPosition()) / (MAX_TURNS * entityCount), 0.5));
 *       return Math.max(t, f);
 *   }
 */
double lw_state_get_progress(LwState *self) {
    /* In Java `order == null` is not a meaningful check here -- it's
     * always an Order object after init().  We mirror the conditional
     * by checking turn==0 to avoid divide-by-zero. */
    int entityCount = self->order.n_leeks;
    if (entityCount == 0) return 0;

    double f = get_faction_progress(self);
    double num = (double)(lw_state_get_turn(self) * entityCount + self->order.position);
    double den = (double)(LW_FIGHT_MAX_TURNS * entityCount);
    double t = lw_min_d(1.0, pow(num / den, 0.5));
    return lw_max_d(t, f);
}
