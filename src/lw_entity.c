/*
 * lw_entity.c -- 1:1 port of state/Entity.java (the abstract base).
 *
 * One C struct holds the union of fields needed by every subtype. The
 * subtype tag (`type`) drives the few overrideable hooks (getType,
 * isSummon, getSummoner, getLeek, startFight) and is set by the
 * subtype-specific init function in lw_leek.c / lw_bulb.c / lw_turret.c.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/Entity.java
 */
#include "lw_entity.h"
#include "lw_effect.h"
#include "lw_effect_params.h"
#include "lw_actions.h"
#include "lw_pathfinding.h"
#include "lw_constants.h"
#include "lw_util.h"
#include "lw_rng.h"
#include "lw_state.h"
#include "lw_statistics.h"
#include "lw_attack.h"
#include "lw_weapon.h"
#include "lw_chip.h"

#include <math.h>
#include <stddef.h>
#include <string.h>


/* ---- Forward decls for engine APIs we call ------------------------ *
 *
 * Most modules we depend on are now included above directly; the few
 * functions still missing in those headers are forward-declared here.
 *
 * NOTE: lw_effect.h provides the inline accessors (lw_effect_get_id /
 * get_turns / set_turns / etc.) -- we use those directly. Same for
 * lw_effect_params.h, lw_attack.h, lw_weapon.h, lw_chip.h.
 *
 * The lw_state_statistics_* helpers wrap state->statistics-> dispatch
 * (see the Java line `state.statistics.X`); we forward-declare them
 * here because they are not all exposed as inline dispatchers in
 * lw_statistics.h. They will be implemented in lw_state.c as thin
 * wrappers around state->statistics-> calls. */

void  lw_state_statistics_damage           (struct LwState *state, struct LwEntity *target,
                                            struct LwEntity *attacker, int pv, int damage_type,
                                            struct LwEffect *effect);
void  lw_state_statistics_heal             (struct LwState *state, struct LwEntity *healer,
                                            struct LwEntity *target, int pv);
void  lw_state_statistics_vitality         (struct LwState *state, struct LwEntity *target,
                                            struct LwEntity *caster, int vitality);
void  lw_state_statistics_characteristics  (struct LwState *state, struct LwEntity *entity);
void  lw_state_statistics_update_stat      (struct LwState *state, struct LwEntity *entity,
                                            int id, int delta, struct LwEntity *caster);
void  lw_state_statistics_use_tp           (struct LwState *state, int tp);
void  lw_state_statistics_entity_turn      (struct LwState *state, struct LwEntity *entity);
void  lw_state_statistics_antidote         (struct LwState *state, struct LwEntity *target,
                                            struct LwEntity *caster, int poisons_removed);


/* MAX_TURNS constant (state/State.MAX_TURNS) */
#define LW_ENTITY_MAX_TURNS_PLUS_2  (LW_FIGHT_MAX_TURNS + 2)


/* ---- Internal string helpers --------------------------------------- */

static void lw_str_copy_n(char *dst, const char *src, size_t cap) {
    if (cap == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}


/* ---- Internal cooldown / item-uses helpers (TreeMap-equivalent) --- *
 *
 * Keys stay sorted ascending so a linear walk matches Java's TreeMap
 * iteration order. Insert is O(n); n is bounded by LW_ENTITY_MAX_*. */

static int lw_iie_find(const LwIntIntEntry *arr, int n, int key) {
    for (int i = 0; i < n; i++) {
        if (arr[i].present && arr[i].key == key) return i;
    }
    return -1;
}

/* Insert / overwrite key, keeping the array sorted ascending by key. */
static void lw_iie_put(LwIntIntEntry *arr, int *pn, int cap, int key, int value) {
    int idx = lw_iie_find(arr, *pn, key);
    if (idx >= 0) {
        arr[idx].value = value;
        return;
    }
    /* Find insertion position (sorted ascending). */
    int pos = *pn;
    for (int i = 0; i < *pn; i++) {
        if (key < arr[i].key) { pos = i; break; }
    }
    if (*pn >= cap) return;  /* defensive: drop if over capacity */
    /* Shift right. */
    for (int i = *pn; i > pos; i--) arr[i] = arr[i - 1];
    arr[pos].key = key;
    arr[pos].value = value;
    arr[pos].present = 1;
    (*pn)++;
}

static void lw_iie_remove(LwIntIntEntry *arr, int *pn, int key) {
    int idx = lw_iie_find(arr, *pn, key);
    if (idx < 0) return;
    for (int i = idx; i < *pn - 1; i++) arr[i] = arr[i + 1];
    (*pn)--;
    arr[*pn].present = 0;
}


/* ---- Effect array helpers ------------------------------------------ */

/* Find pointer index in array; returns -1 if absent. */
static int lw_effect_arr_find(struct LwEffect **arr, int n, struct LwEffect *e) {
    for (int i = 0; i < n; i++) {
        if (arr[i] == e) return i;
    }
    return -1;
}

/* Append. */
static void lw_effect_arr_add(struct LwEffect **arr, int *pn, int cap, struct LwEffect *e) {
    if (*pn >= cap) return;
    arr[*pn] = e;
    (*pn)++;
}

/* Remove first occurrence; preserves order. */
static void lw_effect_arr_remove(struct LwEffect **arr, int *pn, struct LwEffect *e) {
    int idx = lw_effect_arr_find(arr, *pn, e);
    if (idx < 0) return;
    for (int i = idx; i < *pn - 1; i++) arr[i] = arr[i + 1];
    (*pn)--;
    arr[*pn] = NULL;
}


/* ---- Default-init helpers (non-null fields) ------------------------ */

static void lw_entity_zero_init(LwEntity *self) {
    /* Wipe everything to a known state, then layer Java's field initialisers. */
    memset(self, 0, sizeof(*self));

    self->type = LW_ENTITY_TYPE_LEEK;       /* default; subclass overrides */
    self->m_birth_turn = 1;                 /* protected int mBirthTurn = 1 */
    self->orientation = -1;                 /* private int orientation = -1 */
    self->initial_cell = 0;
    self->initial_cell_set = 0;
    self->m_hat = 0;
    self->m_farmer_country_set = 0;
    self->m_composition_name_set = 0;
    self->m_team_name[0] = '\0';
    self->m_farmer_name[0] = '\0';
    self->m_ai_name[0] = '\0';
    self->name[0] = '\0';
    lw_stats_init(&self->m_base_stats);
    lw_stats_init(&self->m_buff_stats);
}


/* ---- Constructors -------------------------------------------------- */

/* Java: public Entity() { this(0, ""); } */
void lw_entity_init_default(LwEntity *self) {
    lw_entity_init_id_name(self, 0, "");
}


/* Java: public Entity(Integer id, String name) {
 *     mId = id;
 *     this.name = name;
 *     mLevel = 1;
 *     mFarmer = 0;
 *     mSkin = 0;
 *     mHat = -1;
 *
 *     mBuffStats = new Stats();
 *     mBaseStats = new Stats();
 *     mBaseStats.setStat(STAT_LIFE, 0);
 *     ... all stats to 0 ...
 *
 *     mTotalLife = mBaseStats.getStat(STAT_LIFE);
 *     this.life = mTotalLife;
 *
 *     mWeapons = new ArrayList<Weapon>();
 *
 *     endTurn();
 * }
 */
void lw_entity_init_id_name(LwEntity *self, int id, const char *name) {
    lw_entity_zero_init(self);

    self->m_id = id;
    lw_str_copy_n(self->name, name, sizeof(self->name));
    self->m_level = 1;
    self->m_farmer = 0;
    self->m_skin = 0;
    self->m_hat = -1;

    /* All base stats default to 0 (already zeroed by zero_init). */
    self->m_total_life = lw_stats_get_stat(&self->m_base_stats, LW_STAT_LIFE);
    self->life = self->m_total_life;

    /* mWeapons = new ArrayList<>(); -- already n_weapons = 0 */

    lw_entity_end_turn(self);
}


/* Java: public Entity(Integer id, String name, int farmer, int level,
 *                     int life, int turn_point, int move_point, int force,
 *                     int agility, int frequency, int wisdom, int resistance,
 *                     int science, int magic, int cores, int ram, int skin,
 *                     boolean metal, int face, int team_id, String team_name,
 *                     int ai_id, String ai_name, String farmer_name,
 *                     String farmer_country, int hat) { ... }
 */
void lw_entity_init_full(LwEntity *self, int id, const char *name, int farmer,
                         int level, int life, int turn_point, int move_point,
                         int force, int agility, int frequency, int wisdom,
                         int resistance, int science, int magic, int cores, int ram,
                         int skin, int metal, int face, int team_id,
                         const char *team_name, int ai_id, const char *ai_name,
                         const char *farmer_name, const char *farmer_country, int hat) {

    lw_entity_zero_init(self);

    self->m_id = id;
    lw_str_copy_n(self->name, name, sizeof(self->name));
    self->m_level = level;
    self->m_farmer = farmer;
    self->m_skin = skin;
    self->m_hat = hat;
    self->m_metal = metal ? 1 : 0;
    self->m_face = face;

    lw_stats_set_stat(&self->m_base_stats, LW_STAT_LIFE, life);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_TP, turn_point);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_MP, move_point);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_STRENGTH, force);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_AGILITY, agility);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_FREQUENCY, frequency);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_WISDOM, wisdom);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_RESISTANCE, resistance);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_SCIENCE, science);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_MAGIC, magic);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_CORES, cores);
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_RAM, ram);

    self->m_total_life = lw_stats_get_stat(&self->m_base_stats, LW_STAT_LIFE);
    self->m_initial_life = self->m_total_life;
    self->life = self->m_total_life;

    /* mWeapons = new ArrayList<>(); -- already n_weapons = 0 */

    lw_str_copy_n(self->m_team_name, team_name, sizeof(self->m_team_name));
    self->m_team_id = team_id;
    lw_str_copy_n(self->m_farmer_name, farmer_name, sizeof(self->m_farmer_name));
    if (farmer_country != NULL) {
        lw_str_copy_n(self->m_farmer_country, farmer_country, sizeof(self->m_farmer_country));
        self->m_farmer_country_set = 1;
    }
    lw_str_copy_n(self->m_ai_name, ai_name, sizeof(self->m_ai_name));
    self->m_ai_id = ai_id;

    lw_entity_end_turn(self);
}


/* Java: public Entity(Entity entity) { ... copy ctor ... }
 *
 * Notes from Java source:
 *   - mWeapons / mChips / passiveEffects are aliased (immutable references).
 *   - effects / launchedEffects are explicitly NOT copied (Java comment).
 *   - mCooldown is deep-copied via TreeMap copy ctor.
 */
void lw_entity_init_copy(LwEntity *self, const LwEntity *entity) {

    lw_entity_zero_init(self);

    self->m_id = entity->m_id;
    self->fight_id = entity->fight_id;
    lw_str_copy_n(self->name, entity->name, sizeof(self->name));
    self->team = entity->team;
    self->m_level = entity->m_level;
    self->m_farmer = entity->m_farmer;
    lw_stats_copy(&self->m_buff_stats, &entity->m_buff_stats);
    lw_stats_copy(&self->m_base_stats, &entity->m_base_stats);
    self->m_initial_life = entity->m_initial_life;
    self->m_total_life = entity->m_total_life;
    self->m_static = entity->m_static;
    self->says_turn = entity->says_turn;
    self->shows_turn = entity->shows_turn;
    self->resurrected = entity->resurrected;
    self->cell = entity->cell;
    self->life = entity->life;

    /* mWeapons aliased (immutable in Java). */
    self->n_weapons = entity->n_weapons;
    for (int i = 0; i < entity->n_weapons; i++) self->m_weapons[i] = entity->m_weapons[i];

    /* mChips aliased. */
    self->n_chips = entity->n_chips;
    for (int i = 0; i < entity->n_chips; i++) {
        self->m_chips[i] = entity->m_chips[i];
        self->m_chip_ids[i] = entity->m_chip_ids[i];
    }

    self->weapon = entity->weapon;

    /* mCooldown = new TreeMap<>(entity.mCooldown); */
    self->n_cooldowns = entity->n_cooldowns;
    for (int i = 0; i < entity->n_cooldowns; i++) {
        self->m_cooldown[i] = entity->m_cooldown[i];
    }

    self->used_tp = entity->used_tp;
    self->used_mp = entity->used_mp;

    /* passiveEffects aliased (immutable). */
    self->n_passive_effects = entity->n_passive_effects;
    for (int i = 0; i < entity->n_passive_effects; i++) {
        self->passive_effects[i] = entity->passive_effects[i];
    }

    /* effects / launchedEffects intentionally NOT copied -- Java comment:
     *   // protected final ArrayList<Effect> effects = new ArrayList<Effect>();
     *   // private final ArrayList<Effect> launchedEffects = new ArrayList<Effect>();
     */
}


/* ---- Type / identity getters --------------------------------------- */

/* Java: public abstract int getType(); -- subclass override returns the constant. */
int lw_entity_get_type(const LwEntity *self) { return self->type; }

/* Java: public Leek getLeek() { return null; } / Leek overrides -> this. */
LwEntity* lw_entity_get_leek(LwEntity *self) {
    return (self->type == LW_ENTITY_TYPE_LEEK) ? self : NULL;
}

/* Java: public boolean isSummon() { return false; } / Bulb overrides -> true. */
int lw_entity_is_summon(const LwEntity *self) {
    return (self->type == LW_ENTITY_TYPE_BULB) ? 1 : 0;
}

/* Java: public Entity getSummoner() { return null; } / Bulb overrides -> mOwner. */
LwEntity* lw_entity_get_summoner(const LwEntity *self) {
    if (self->type == LW_ENTITY_TYPE_BULB) return self->m_owner;
    return NULL;
}

/* Java: public int getFId() */
int lw_entity_get_fid(const LwEntity *self) { return self->fight_id; }

/* Java: public int getId() / setId(int) */
int  lw_entity_get_id(const LwEntity *self) { return self->m_id; }
void lw_entity_set_id(LwEntity *self, int id) { self->m_id = id; }

/* Java: public int getLevel() / setLevel(int) */
int  lw_entity_get_level(const LwEntity *self) { return self->m_level; }
void lw_entity_set_level(LwEntity *self, int level) { self->m_level = level; }

/* Java: public int getFarmer() / setFarmer(int) */
int  lw_entity_get_farmer(const LwEntity *self) { return self->m_farmer; }
void lw_entity_set_farmer(LwEntity *self, int farmer) { self->m_farmer = farmer; }

/* Java: public String getName() / setName(String) */
const char* lw_entity_get_name(const LwEntity *self) { return self->name; }
void        lw_entity_set_name(LwEntity *self, const char *name) {
    lw_str_copy_n(self->name, name, sizeof(self->name));
}

/* Java: public int getTeam() / setTeam(int) */
int  lw_entity_get_team(const LwEntity *self) { return self->team; }
void lw_entity_set_team(LwEntity *self, int team) { self->team = team; }

/* Java: public int getTeamId() / setTeamID(int) */
int  lw_entity_get_team_id(const LwEntity *self) { return self->m_team_id; }
void lw_entity_set_team_id(LwEntity *self, int team) { self->m_team_id = team; }

/* Java: public String getTeamName() / setTeamName(String) */
const char* lw_entity_get_team_name(const LwEntity *self) { return self->m_team_name; }
void        lw_entity_set_team_name(LwEntity *self, const char *name) {
    lw_str_copy_n(self->m_team_name, name, sizeof(self->m_team_name));
}

/* Java: public String getCompositionName() { return mCompositionName; } / setCompositionName */
const char* lw_entity_get_composition_name(const LwEntity *self) {
    return self->m_composition_name_set ? self->m_composition_name : NULL;
}
void lw_entity_set_composition_name(LwEntity *self, const char *name) {
    if (name == NULL) {
        self->m_composition_name_set = 0;
        self->m_composition_name[0] = '\0';
    } else {
        lw_str_copy_n(self->m_composition_name, name, sizeof(self->m_composition_name));
        self->m_composition_name_set = 1;
    }
}

/* Java: public String getAIName() / setAIName(String) */
const char* lw_entity_get_ai_name(const LwEntity *self) { return self->m_ai_name; }
void        lw_entity_set_ai_name(LwEntity *self, const char *ai) {
    lw_str_copy_n(self->m_ai_name, ai, sizeof(self->m_ai_name));
}

/* Java: public int getAIId() */
int lw_entity_get_ai_id(const LwEntity *self) { return self->m_ai_id; }

/* Java: public String getFarmerName() / setFarmerName */
const char* lw_entity_get_farmer_name(const LwEntity *self) { return self->m_farmer_name; }
void        lw_entity_set_farmer_name(LwEntity *self, const char *name) {
    lw_str_copy_n(self->m_farmer_name, name, sizeof(self->m_farmer_name));
}

/* Java: public String getFarmerCountry() {
 *           if (mFarmerCountry == null) return "?";
 *           return mFarmerCountry;
 *       }
 */
const char* lw_entity_get_farmer_country(const LwEntity *self) {
    if (!self->m_farmer_country_set) return "?";
    return self->m_farmer_country;
}
void lw_entity_set_farmer_country(LwEntity *self, const char *country) {
    if (country == NULL) {
        self->m_farmer_country_set = 0;
        self->m_farmer_country[0] = '\0';
    } else {
        lw_str_copy_n(self->m_farmer_country, country, sizeof(self->m_farmer_country));
        self->m_farmer_country_set = 1;
    }
}

/* Java: getHat / setHat / getSkin / setSkin / getMetal / setMetal / getFace / setFace */
int  lw_entity_get_hat (const LwEntity *self) { return self->m_hat; }
void lw_entity_set_hat (LwEntity *self, int hat) { self->m_hat = hat; }
int  lw_entity_get_skin(const LwEntity *self) { return self->m_skin; }
void lw_entity_set_skin(LwEntity *self, int skin) { self->m_skin = skin; }
int  lw_entity_get_metal(const LwEntity *self) { return self->m_metal; }
void lw_entity_set_metal(LwEntity *self, int metal) { self->m_metal = metal ? 1 : 0; }
int  lw_entity_get_face(const LwEntity *self) { return self->m_face; }
void lw_entity_set_face(LwEntity *self, int face) { self->m_face = face; }


/* ---- Cell / orientation -------------------------------------------- */

LwCell* lw_entity_get_cell(const LwEntity *self) { return self->cell; }
void    lw_entity_set_cell(LwEntity *self, LwCell *cell) { self->cell = cell; }

int  lw_entity_has_initial_cell(const LwEntity *self) { return self->initial_cell_set; }
int  lw_entity_get_initial_cell(const LwEntity *self) { return self->initial_cell; }
void lw_entity_set_initial_cell(LwEntity *self, int cell) {
    self->initial_cell = cell;
    self->initial_cell_set = 1;
}

int  lw_entity_get_orientation(const LwEntity *self) { return self->orientation; }
void lw_entity_set_orientation(LwEntity *self, int orientation) {
    self->orientation = orientation;
}


/* ---- Stats --------------------------------------------------------- */

LwStats* lw_entity_get_base_stats(LwEntity *self) { return &self->m_base_stats; }

/* Java: public int getStat(int id) {
 *     return mBaseStats.getStat(id) + mBuffStats.getStat(id);
 * }
 */
int lw_entity_get_stat(const LwEntity *self, int id) {
    return lw_stats_get_stat(&self->m_base_stats, id)
         + lw_stats_get_stat(&self->m_buff_stats, id);
}

int lw_entity_get_strength      (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_STRENGTH); }
int lw_entity_get_agility       (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_AGILITY); }
int lw_entity_get_resistance    (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_RESISTANCE); }
int lw_entity_get_science       (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_SCIENCE); }
int lw_entity_get_magic         (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_MAGIC); }
int lw_entity_get_wisdom        (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_WISDOM); }
int lw_entity_get_relative_shield(const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_RELATIVE_SHIELD); }
int lw_entity_get_absolute_shield(const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_ABSOLUTE_SHIELD); }
int lw_entity_get_damage_return (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_DAMAGE_RETURN); }
int lw_entity_get_frequency     (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_FREQUENCY); }
int lw_entity_get_cores         (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_CORES); }
int lw_entity_get_ram           (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_RAM); }
int lw_entity_get_total_tp      (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_TP); }
int lw_entity_get_total_mp      (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_MP); }
int lw_entity_get_power         (const LwEntity *self) { return lw_entity_get_stat(self, LW_STAT_POWER); }

/* Java: public int getMP() { return getTotalMP() - usedMP; } */
int lw_entity_get_mp(const LwEntity *self) {
    return lw_entity_get_total_mp(self) - self->used_mp;
}

/* Java: public int getTP() { return getTotalTP() - usedTP; } */
int lw_entity_get_tp(const LwEntity *self) {
    return lw_entity_get_total_tp(self) - self->used_tp;
}

/* Java: setLife(int) -- writes both base stat AND life field. */
void lw_entity_set_life(LwEntity *self, int life) {
    lw_stats_set_stat(&self->m_base_stats, LW_STAT_LIFE, life);
    self->life = life;
}

void lw_entity_set_strength   (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_STRENGTH, v); }
void lw_entity_set_agility    (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_AGILITY, v); }
void lw_entity_set_wisdom     (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_WISDOM, v); }
void lw_entity_set_resistance (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_RESISTANCE, v); }
void lw_entity_set_science    (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_SCIENCE, v); }
void lw_entity_set_magic      (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_MAGIC, v); }
void lw_entity_set_frequency  (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_FREQUENCY, v); }
void lw_entity_set_cores      (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_CORES, v); }
void lw_entity_set_ram        (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_RAM, v); }
void lw_entity_set_tp         (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_TP, v); }
void lw_entity_set_mp         (LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_MP, v); }
void lw_entity_set_relative_shield(LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_RELATIVE_SHIELD, v); }
void lw_entity_set_absolute_shield(LwEntity *self, int v) { lw_stats_set_stat(&self->m_base_stats, LW_STAT_ABSOLUTE_SHIELD, v); }


/* ---- Buff stats ---------------------------------------------------- */

/* Java: public void updateBuffStats() {
 *     mBuffStats.clear();
 *     states.clear();
 *     for (Effect effect : effects) {
 *         if (effect.getStats() != null)
 *             mBuffStats.addStats(effect.getStats());
 *         if (effect.getState() != null) {
 *             states.add(effect.getState());
 *         }
 *     }
 * }
 *
 * NOTE: in C, getStats() always returns a valid pointer (never NULL),
 * so we always call addStats. getState() returns LW_ENTITY_STATE_NONE
 * when Java's enum was null -- treated as "no state" here.
 */
void lw_entity_update_buff_stats(LwEntity *self) {
    lw_stats_clear(&self->m_buff_stats);
    self->states = 0;
    for (int i = 0; i < self->n_effects; i++) {
        struct LwEffect *effect = self->effects[i];
        LwStats *es = lw_effect_get_stats(effect);
        if (es != NULL) {
            lw_stats_add_stats(&self->m_buff_stats, es);
        }
        int s = lw_effect_get_state(effect);
        if (s != LW_ENTITY_STATE_NONE) {
            self->states |= (1u << s);
        }
    }
}

/* Java: public void updateBuffStats(int id, int delta, Entity caster) {
 *     mBuffStats.updateStat(id, delta);
 *     state.statistics.characteristics(this);
 *     state.statistics.updateStat(this, id, delta, caster);
 * }
 */
void lw_entity_update_buff_stats_with(LwEntity *self, int id, int delta, LwEntity *caster) {
    lw_stats_update_stat(&self->m_buff_stats, id, delta);
    lw_state_statistics_characteristics(self->state, self);
    lw_state_statistics_update_stat(self->state, self, id, delta, caster);
}


/* ---- Life / damage ------------------------------------------------- */

int lw_entity_get_life(const LwEntity *self) { return self->life; }
int lw_entity_get_total_life(const LwEntity *self) { return self->m_total_life; }
int lw_entity_get_initial_life(const LwEntity *self) { return self->m_initial_life; }

/* Java: public void addTotalLife(int vitality, Entity caster) {
 *     mTotalLife += vitality;
 *     state.statistics.vitality(this, caster, vitality);
 * }
 */
void lw_entity_add_total_life(LwEntity *self, int vitality, LwEntity *caster) {
    self->m_total_life += vitality;
    lw_state_statistics_vitality(self->state, self, caster, vitality);
}

/* Java: public void setTotalLife(int vitality) {
 *     mTotalLife = vitality;
 *     mInitialLife = vitality;
 * }
 */
void lw_entity_set_total_life(LwEntity *self, int vitality) {
    self->m_total_life = vitality;
    self->m_initial_life = vitality;
}

/* Java: public boolean isDead() { return life <= 0; } */
int lw_entity_is_dead(const LwEntity *self) { return self->life <= 0 ? 1 : 0; }

/* Java: public boolean isAlive() { return !isDead(); } */
int lw_entity_is_alive(const LwEntity *self) { return !lw_entity_is_dead(self); }

/* Java: public void setDead(boolean dead) { if (dead) this.life = 0; } */
void lw_entity_set_dead(LwEntity *self, int dead) {
    if (dead) self->life = 0;
}


/* Java: public void removeLife(int pv, int erosion, Entity attacker, DamageType type, Effect effect, Item item) {
 *     if (isDead()) return;
 *
 *     if (pv > life) { pv = life; }
 *     life -= pv;
 *
 *     // Add erosion
 *     mTotalLife -= erosion;
 *     if (mTotalLife < 1) mTotalLife = 1;
 *
 *     if (pv > 0) {
 *         state.statistics.damage(this, attacker, pv, type, effect);
 *     }
 *     if (erosion > 0) {
 *         state.statistics.damage(this, attacker, erosion, DamageType.NOVA, effect);
 *     }
 *
 *     if (life <= 0) {
 *         state.onPlayerDie(this, attacker, item);
 *         die();
 *     }
 * }
 *
 * NOTE: the order matters -- statistics.damage is called BEFORE die(),
 * which itself fires more action stream entries (effect removals).
 */
void lw_entity_remove_life(LwEntity *self, int pv, int erosion,
                           LwEntity *attacker, int damage_type,
                           struct LwEffect *effect, struct LwItem *item) {

    if (lw_entity_is_dead(self)) return;

    if (pv > self->life) { pv = self->life; }
    self->life -= pv;

    /* Add erosion */
    self->m_total_life -= erosion;
    if (self->m_total_life < 1) self->m_total_life = 1;

    if (pv > 0) {
        lw_state_statistics_damage(self->state, self, attacker, pv, damage_type, effect);
    }
    if (erosion > 0) {
        /* DamageType.NOVA = 107 -- see attack/DamageType.java */
        lw_state_statistics_damage(self->state, self, attacker, erosion, LW_ACTION_NOVA_DAMAGE, effect);
    }

    if (self->life <= 0) {
        lw_state_on_player_die(self->state, self, attacker, item);
        lw_entity_die(self);
    }
}


/* Java: public void addLife(Entity healer, int pv) {
 *     if (pv > getTotalLife() - life) {
 *         pv = getTotalLife() - life;
 *     }
 *     life += pv;
 *     state.statistics.heal(healer, this, pv);
 *     state.statistics.characteristics(this);
 * }
 */
void lw_entity_add_life(LwEntity *self, LwEntity *healer, int pv) {
    int max_heal = lw_entity_get_total_life(self) - self->life;
    if (pv > max_heal) pv = max_heal;
    self->life += pv;
    lw_state_statistics_heal(self->state, healer, self, pv);
    lw_state_statistics_characteristics(self->state, self);
}


/* Java: public void die() {
 *     life = 0;
 *
 *     // Remove launched effects
 *     while (launchedEffects.size() > 0) {
 *         Effect effect = launchedEffects.get(0);
 *         effect.getTarget().removeEffect(effect);
 *         launchedEffects.remove(0);
 *     }
 *
 *     // Remove effects
 *     while (effects.size() > 0) {
 *         var effect = effects.get(0);
 *         effect.getCaster().removeLaunchedEffect(effect);
 *         effects.remove(0); // Don't send remove effect action, the client will remove the effects itself
 *     }
 *     updateBuffStats();
 *
 *     // Kill summons
 *     List<Entity> entities = new ArrayList<Entity>(state.getTeamEntities(getTeam()));
 *     for (Entity e : entities) {
 *         if (e.isSummon() && e.getSummoner().getFId() == getFId()) {
 *             state.onPlayerDie(e, null, null);
 *             e.die();
 *         }
 *     }
 * }
 */
void lw_entity_die(LwEntity *self) {

    self->life = 0;

    /* Remove launched effects -- iterate while non-empty. */
    while (self->n_launched_effects > 0) {
        struct LwEffect *effect = self->launched_effects[0];
        LwEntity *target = lw_effect_get_target(effect);
        if (target != NULL) {
            lw_entity_remove_effect(target, effect);
        }
        /* launchedEffects.remove(0) */
        for (int i = 0; i < self->n_launched_effects - 1; i++) {
            self->launched_effects[i] = self->launched_effects[i + 1];
        }
        self->n_launched_effects--;
        self->launched_effects[self->n_launched_effects] = NULL;
    }

    /* Remove effects (silent: no remove-effect action) */
    while (self->n_effects > 0) {
        struct LwEffect *effect = self->effects[0];
        LwEntity *caster = lw_effect_get_caster(effect);
        if (caster != NULL) {
            lw_entity_remove_launched_effect(caster, effect);
        }
        /* effects.remove(0) */
        for (int i = 0; i < self->n_effects - 1; i++) {
            self->effects[i] = self->effects[i + 1];
        }
        self->n_effects--;
        self->effects[self->n_effects] = NULL;
    }
    lw_entity_update_buff_stats(self);

    /* Kill summons */
    LwEntity *entities[64];
    int n = lw_state_get_team_entities(self->state, lw_entity_get_team(self), entities, 64);
    for (int i = 0; i < n; i++) {
        LwEntity *e = entities[i];
        LwEntity *sm = lw_entity_get_summoner(e);
        if (lw_entity_is_summon(e) && sm != NULL && lw_entity_get_fid(sm) == lw_entity_get_fid(self)) {
            lw_state_on_player_die(self->state, e, NULL, NULL);
            lw_entity_die(e);
        }
    }
}


/* ---- Passive damage hooks ------------------------------------------ *
 *
 * Java: public void onDirectDamage(int damage) {
 *     if (isDead()) return;
 *     for (Weapon weapon : mWeapons) {
 *         for (EffectParameters effect : weapon.getPassiveEffects()) {
 *             activateOnDamagePassiveEffect(effect, weapon.getAttack(), damage);
 *         }
 *     }
 * }
 *
 * Same shape for onNovaDamage / onPoisonDamage / onMoved / onAllyKilled /
 * onCritical / onKill -- only the inner activator differs.
 */

void lw_entity_on_direct_damage(LwEntity *self, int damage) {
    if (lw_entity_is_dead(self)) return;
    for (int w = 0; w < self->n_weapons; w++) {
        struct LwWeapon *weapon = self->m_weapons[w];
        int npe = lw_weapon_get_passive_effects_count(weapon);
        for (int e = 0; e < npe; e++) {
            const LwEffectParameters *effect = &lw_weapon_get_passive_effects(weapon)[e];
            lw_entity_activate_on_damage_passive_effect(self, effect,
                                                        lw_weapon_get_attack(weapon), damage);
        }
    }
}

void lw_entity_on_nova_damage(LwEntity *self, int damage) {
    if (lw_entity_is_dead(self)) return;
    for (int w = 0; w < self->n_weapons; w++) {
        struct LwWeapon *weapon = self->m_weapons[w];
        int npe = lw_weapon_get_passive_effects_count(weapon);
        for (int e = 0; e < npe; e++) {
            const LwEffectParameters *effect = &lw_weapon_get_passive_effects(weapon)[e];
            lw_entity_activate_on_nova_damage_passive_effect(self, effect,
                                                             lw_weapon_get_attack(weapon), damage);
        }
    }
}

void lw_entity_on_poison_damage(LwEntity *self, int damage) {
    if (lw_entity_is_dead(self)) return;
    for (int w = 0; w < self->n_weapons; w++) {
        struct LwWeapon *weapon = self->m_weapons[w];
        int npe = lw_weapon_get_passive_effects_count(weapon);
        for (int e = 0; e < npe; e++) {
            const LwEffectParameters *effect = &lw_weapon_get_passive_effects(weapon)[e];
            lw_entity_activate_on_poison_damage_passive_effect(self, effect,
                                                               lw_weapon_get_attack(weapon), damage);
        }
    }
}

/* Java: public void onMoved(Entity by) {
 *     if (isDead()) return;
 *     if (by == this) return; // Déplacement subi uniquement
 *     for (Weapon weapon : mWeapons) {
 *         for (EffectParameters effect : weapon.getPassiveEffects()) {
 *             activateOnMovedPassiveEffect(effect, weapon.getAttack());
 *         }
 *     }
 * }
 */
void lw_entity_on_moved(LwEntity *self, LwEntity *by) {
    if (lw_entity_is_dead(self)) return;
    if (by == self) return;  /* Déplacement subi uniquement */
    for (int w = 0; w < self->n_weapons; w++) {
        struct LwWeapon *weapon = self->m_weapons[w];
        int npe = lw_weapon_get_passive_effects_count(weapon);
        for (int e = 0; e < npe; e++) {
            const LwEffectParameters *effect = &lw_weapon_get_passive_effects(weapon)[e];
            lw_entity_activate_on_moved_passive_effect(self, effect, lw_weapon_get_attack(weapon));
        }
    }
}

void lw_entity_on_ally_killed(LwEntity *self) {
    if (lw_entity_is_dead(self)) return;
    for (int w = 0; w < self->n_weapons; w++) {
        struct LwWeapon *weapon = self->m_weapons[w];
        int npe = lw_weapon_get_passive_effects_count(weapon);
        for (int e = 0; e < npe; e++) {
            const LwEffectParameters *effect = &lw_weapon_get_passive_effects(weapon)[e];
            lw_entity_activate_on_ally_killed_passive_effect(self, effect, lw_weapon_get_attack(weapon));
        }
    }
}

void lw_entity_on_critical(LwEntity *self) {
    if (lw_entity_is_dead(self)) return;
    for (int w = 0; w < self->n_weapons; w++) {
        struct LwWeapon *weapon = self->m_weapons[w];
        int npe = lw_weapon_get_passive_effects_count(weapon);
        for (int e = 0; e < npe; e++) {
            const LwEffectParameters *effect = &lw_weapon_get_passive_effects(weapon)[e];
            lw_entity_activate_on_critical_passive_effect(self, effect, lw_weapon_get_attack(weapon));
        }
    }
}

void lw_entity_on_kill(LwEntity *self) {
    if (lw_entity_is_dead(self)) return;
    for (int w = 0; w < self->n_weapons; w++) {
        struct LwWeapon *weapon = self->m_weapons[w];
        int npe = lw_weapon_get_passive_effects_count(weapon);
        for (int e = 0; e < npe; e++) {
            const LwEffectParameters *effect = &lw_weapon_get_passive_effects(weapon)[e];
            lw_entity_activate_on_kill_passive_effect(self, effect, lw_weapon_get_attack(weapon));
        }
    }
}


/* ---- Passive effect activators ------------------------------------ *
 *
 * Each one inspects the effect.id and conditionally fires a
 * createEffect call. Unrecognised ids are no-ops -- matching Java's
 * if/else chains exactly.
 */

/* Java: public void activateOnMovedPassiveEffect(EffectParameters effect, Attack attack) {
 *     if (effect.getId() == Effect.TYPE_MOVED_TO_MP) {
 *         double value = effect.getValue1();
 *         boolean stackable = (effect.getModifiers() & Effect.MODIFIER_STACKABLE) != 0;
 *         Effect.createEffect(this.state, Effect.TYPE_RAW_BUFF_MP, effect.getTurns(), 1,
 *             value, 0, false, this, this, attack, 0, stackable, 0, 1, 0, effect.getModifiers());
 *     }
 * }
 */
void lw_entity_activate_on_moved_passive_effect(LwEntity *self,
                                                const LwEffectParameters *effect,
                                                struct LwAttack *attack) {
    if (lw_effect_parameters_get_id(effect) == 50 /* TYPE_MOVED_TO_MP */) {
        double value = lw_effect_parameters_get_value1(effect);
        int stackable = (lw_effect_parameters_get_modifiers(effect) & LW_MODIFIER_STACKABLE) != 0;
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_BUFF_MP,
                                  lw_effect_parameters_get_turns(effect), 1,
                                  value, 0, 0, self, self, attack, 0, stackable,
                                  0, 1, 0, lw_effect_parameters_get_modifiers(effect));
    }
}

/* Java: public void activateOnDamagePassiveEffect(EffectParameters effect, Attack attack, int inputValue) {
 *     if (effect.getId() == Effect.TYPE_DAMAGE_TO_ABSOLUTE_SHIELD) { ... }
 *     else if (effect.getId() == Effect.TYPE_DAMAGE_TO_STRENGTH) { ... }
 * }
 */
void lw_entity_activate_on_damage_passive_effect(LwEntity *self,
                                                 const LwEffectParameters *effect,
                                                 struct LwAttack *attack, int input_value) {
    int eid = lw_effect_parameters_get_id(effect);
    if (eid == 34 /* TYPE_DAMAGE_TO_ABSOLUTE_SHIELD */) {
        double value = input_value * (lw_effect_parameters_get_value1(effect) / 100.0);
        int stackable = (lw_effect_parameters_get_modifiers(effect) & LW_MODIFIER_STACKABLE) != 0;
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_ABSOLUTE_SHIELD,
                                  lw_effect_parameters_get_turns(effect), 1,
                                  value, 0, 0, self, self, attack, 0, stackable,
                                  0, 0, 0, lw_effect_parameters_get_modifiers(effect));
    }
    else if (eid == 35 /* TYPE_DAMAGE_TO_STRENGTH */) {
        double value = input_value * (lw_effect_parameters_get_value1(effect) / 100.0);
        int stackable = (lw_effect_parameters_get_modifiers(effect) & LW_MODIFIER_STACKABLE) != 0;
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_BUFF_STRENGTH,
                                  lw_effect_parameters_get_turns(effect), 1,
                                  value, 0, 0, self, self, attack, 0, stackable,
                                  0, 0, 0, lw_effect_parameters_get_modifiers(effect));
    }
}

/* Java: activateOnNovaDamagePassiveEffect -- TYPE_NOVA_DAMAGE_TO_MAGIC */
void lw_entity_activate_on_nova_damage_passive_effect(LwEntity *self,
                                                      const LwEffectParameters *effect,
                                                      struct LwAttack *attack, int input_value) {
    if (lw_effect_parameters_get_id(effect) == 36 /* TYPE_NOVA_DAMAGE_TO_MAGIC */) {
        double value = input_value * (lw_effect_parameters_get_value1(effect) / 100.0);
        int stackable = (lw_effect_parameters_get_modifiers(effect) & LW_MODIFIER_STACKABLE) != 0;
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_BUFF_MAGIC,
                                  lw_effect_parameters_get_turns(effect), 1,
                                  value, 0, 0, self, self, attack, 0, stackable,
                                  0, 0, 0, lw_effect_parameters_get_modifiers(effect));
    }
}

/* Java: activateOnPoisonDamagePassiveEffect -- TYPE_POISON_TO_SCIENCE */
void lw_entity_activate_on_poison_damage_passive_effect(LwEntity *self,
                                                        const LwEffectParameters *effect,
                                                        struct LwAttack *attack, int input_value) {
    if (lw_effect_parameters_get_id(effect) == 33 /* TYPE_POISON_TO_SCIENCE */) {
        double value = input_value * (lw_effect_parameters_get_value1(effect) / 100.0);
        int stackable = (lw_effect_parameters_get_modifiers(effect) & LW_MODIFIER_STACKABLE) != 0;
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_BUFF_SCIENCE,
                                  lw_effect_parameters_get_turns(effect), 1,
                                  value, 0, 0, self, self, attack, 0, stackable,
                                  0, 0, 0, lw_effect_parameters_get_modifiers(effect));
    }
}

/* Java: activateOnAllyKilledPassiveEffect -- TYPE_ALLY_KILLED_TO_AGILITY */
void lw_entity_activate_on_ally_killed_passive_effect(LwEntity *self,
                                                      const LwEffectParameters *effect,
                                                      struct LwAttack *attack) {
    if (lw_effect_parameters_get_id(effect) == 55 /* TYPE_ALLY_KILLED_TO_AGILITY */) {
        double value = lw_effect_parameters_get_value1(effect);
        int stackable = (lw_effect_parameters_get_modifiers(effect) & LW_MODIFIER_STACKABLE) != 0;
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_BUFF_AGILITY,
                                  lw_effect_parameters_get_turns(effect), 1,
                                  value, 0, 0, self, self, attack, 0, stackable,
                                  0, 0, 0, lw_effect_parameters_get_modifiers(effect));
    }
}

/* Java: activateOnCriticalPassiveEffect -- TYPE_CRITICAL_TO_HEAL
 *
 * NOTE: this branch does state.getRandom().getDouble() -- preserving
 * the RNG draw is critical for action stream parity.
 */
void lw_entity_activate_on_critical_passive_effect(LwEntity *self,
                                                   const LwEffectParameters *effect,
                                                   struct LwAttack *attack) {
    if (lw_effect_parameters_get_id(effect) == 58 /* TYPE_CRITICAL_TO_HEAL */) {
        if (lw_entity_get_life(self) < lw_entity_get_total_life(self)) {
            double value1 = lw_effect_parameters_get_value1(effect);
            double value2 = lw_effect_parameters_get_value2(effect);
            double jet = lw_rng_get_double(lw_state_get_random(self->state));
            lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_HEAL,
                                      0, 1, value1, value2, 0, self, self, attack,
                                      jet, 0, 0, 1, 0,
                                      lw_effect_parameters_get_modifiers(effect));
        }
    }
}

/* Java: activateOnKillPassiveEffect -- TYPE_KILL_TO_TP */
void lw_entity_activate_on_kill_passive_effect(LwEntity *self,
                                               const LwEffectParameters *effect,
                                               struct LwAttack *attack) {
    if (lw_effect_parameters_get_id(effect) == 56 /* TYPE_KILL_TO_TP */) {
        double value = lw_effect_parameters_get_value1(effect);
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_RAW_BUFF_TP,
                                  lw_effect_parameters_get_turns(effect), 1,
                                  value, value, 0, self, self, attack, 0, 1,
                                  0, 1, 0, lw_effect_parameters_get_modifiers(effect));
    }
}


/* ---- Turn lifecycle ----------------------------------------------- */

/* Java: public void startTurn() {
 *     applyCoolDown();
 *     state.statistics.entityTurn(this);
 *
 *     ArrayList<Effect> effectsCopy = new ArrayList<Effect>(this.effects);
 *     for (Effect effect : effectsCopy) {
 *         effect.applyStartTurn(state);
 *         if (isDead()) {
 *             return;
 *         }
 *     }
 *
 *     for (int e = 0; e < launchedEffects.size(); ++e) {
 *         Effect effect = launchedEffects.get(e);
 *         if (effect.getTurns() != -1) { // Decrease duration
 *             effect.setTurns(effect.getTurns() - 1);
 *         }
 *         if (effect.getTurns() == 0) { // Effect done
 *             effect.getTarget().removeEffect(effect);
 *             launchedEffects.remove(e);
 *             e--;
 *         }
 *     }
 * }
 *
 * NOTE: order matters for action stream:
 *   1) applyCoolDown -- mutates cooldowns, no logs
 *   2) statistics.entityTurn -- logs LEEK_TURN
 *   3) effectsCopy iteration -- logs poison damage etc
 *   4) launchedEffects walk -- logs effect removals
 */
void lw_entity_start_turn(LwEntity *self) {

    lw_entity_apply_cool_down(self);

    lw_state_statistics_entity_turn(self->state, self);

    /* Snapshot effects[] before iterating (Java does ArrayList copy ctor). */
    struct LwEffect *effects_copy[LW_ENTITY_MAX_EFFECTS];
    int n_copy = self->n_effects;
    for (int i = 0; i < n_copy; i++) effects_copy[i] = self->effects[i];

    for (int i = 0; i < n_copy; i++) {
        struct LwEffect *effect = effects_copy[i];
        lw_effect_apply_start_turn(effect, self->state);
        if (lw_entity_is_dead(self)) {
            return;
        }
    }

    for (int e = 0; e < self->n_launched_effects; ++e) {
        struct LwEffect *effect = self->launched_effects[e];
        if (lw_effect_get_turns(effect) != -1) {  /* Decrease duration */
            lw_effect_set_turns(effect, lw_effect_get_turns(effect) - 1);
        }
        if (lw_effect_get_turns(effect) == 0) {  /* Effect done */
            LwEntity *target = lw_effect_get_target(effect);
            if (target != NULL) {
                lw_entity_remove_effect(target, effect);
            }
            /* launchedEffects.remove(e); e--; */
            for (int j = e; j < self->n_launched_effects - 1; j++) {
                self->launched_effects[j] = self->launched_effects[j + 1];
            }
            self->n_launched_effects--;
            self->launched_effects[self->n_launched_effects] = NULL;
            e--;
        }
    }
}


/* Java: public void endTurn() {
 *     usedMP = 0;
 *     usedTP = 0;
 *     saysTurn = 0;
 *     showsTurn = 0;
 *     itemUses.clear();
 *
 *     // Propagation des effets
 *     for (Effect effect : effects) {
 *         if (effect.propagate > 0) {
 *             Attack attack = effect.getAttack();
 *             EffectParameters propagation = attack.getEffects().get(0);
 *             EffectParameters original = attack.getEffects().get(1);
 *             double jet = state.getRandom().getDouble();
 *             for (Entity target : getEntitiesAround(effect.propagate)) {
 *                 if ((propagation.getModifiers() & Effect.MODIFIER_NOT_REPLACEABLE) != 0
 *                     && target.hasEffect(attack.getItemId())) {
 *                     continue;
 *                 }
 *                 Effect.createEffect(state, effect.getID(), original.getTurns(), 1,
 *                     original.getValue1(), original.getValue2(), effect.isCritical(),
 *                     target, effect.getCaster(), attack, jet,
 *                     (propagation.getModifiers() & Effect.MODIFIER_STACKABLE) != 0,
 *                     0, 0, effect.propagate, effect.modifiers);
 *             }
 *         }
 *     }
 * }
 */
void lw_entity_end_turn(LwEntity *self) {

    self->used_mp = 0;
    self->used_tp = 0;

    self->says_turn = 0;
    self->shows_turn = 0;

    /* itemUses.clear() */
    for (int i = 0; i < self->n_item_uses; i++) {
        self->item_uses[i].present = 0;
        self->item_uses[i].key = 0;
        self->item_uses[i].value = 0;
    }
    self->n_item_uses = 0;

    /* Propagation des effets -- inner body only runs if effects[] is
     * non-empty. The initial endTurn() called from the constructor has
     * effects[] empty, so state==NULL at that point is harmless. */
    for (int i = 0; i < self->n_effects; i++) {
        struct LwEffect *effect = self->effects[i];
        /* Java: effect.propagate -- public field, no getter. */
        int propagate = effect->propagate;
        if (propagate > 0) {
            struct LwAttack *attack = lw_effect_get_attack(effect);
            /* Java: attack.getEffects().get(0) / get(1) -- direct array access. */
            const LwEffectParameters *propagation = &attack->effects[0];
            const LwEffectParameters *original    = &attack->effects[1];
            double jet = lw_rng_get_double(lw_state_get_random(self->state));

            LwEntity *targets[64];
            int nt = lw_entity_get_entities_around(self, propagate, targets, 64);
            for (int t = 0; t < nt; t++) {
                LwEntity *target = targets[t];
                int prop_mods = lw_effect_parameters_get_modifiers(propagation);
                if ((prop_mods & LW_MODIFIER_NOT_REPLACEABLE) != 0
                        && lw_entity_has_effect(target, lw_attack_get_item_id(attack))) {
                    continue;  /* La cible a déjà l'effet et il n'est pas remplacable */
                }
                int stackable = (prop_mods & LW_MODIFIER_STACKABLE) != 0;
                lw_effect_create_effect(self->state, lw_effect_get_id(effect),
                                          lw_effect_parameters_get_turns(original), 1,
                                          lw_effect_parameters_get_value1(original),
                                          lw_effect_parameters_get_value2(original),
                                          lw_effect_is_critical(effect),
                                          target, lw_effect_get_caster(effect),
                                          attack, jet, stackable,
                                          0, 0, propagate, lw_effect_get_modifiers(effect));
            }
        }
    }
}


/* Java (Entity): public void startFight() { } -- empty in base.
 * Turret overrides to install the static-state effect.
 */
void lw_entity_start_fight(LwEntity *self) {
    if (self->type == LW_ENTITY_TYPE_TURRET) {
        /* Turret.startFight -- add static state via TYPE_ADD_STATE.
         *
         * Effect.createEffect(this.state, Effect.TYPE_ADD_STATE, -1, 1,
         *     EntityState.STATIC.ordinal(), 0, false, this, this, null, 0,
         *     false, 0, 1, 0, Effect.MODIFIER_IRREDUCTIBLE);
         */
        lw_effect_create_effect(self->state, LW_EFFECT_TYPE_ADD_STATE,
                                  -1, 1, (double)LW_ENTITY_STATE_STATIC, 0, 0,
                                  self, self, NULL, 0, 0,
                                  0, 1, 0, LW_MODIFIER_IRREDUCTIBLE);
    }
    /* Other types: no-op. */
}


/* Java: public void applyCoolDown() {
 *     Map<Integer, Integer> cooldown = new TreeMap<Integer, Integer>();
 *     cooldown.putAll(mCooldown);
 *     for (Entry<Integer, Integer> chip : cooldown.entrySet()) {
 *         if (chip.getValue() <= 1)
 *             mCooldown.remove(chip.getKey());
 *         else
 *             mCooldown.put(chip.getKey(), chip.getValue() - 1);
 *     }
 * }
 *
 * NOTE: snapshot before iterating because we mutate mCooldown.
 */
void lw_entity_apply_cool_down(LwEntity *self) {
    LwIntIntEntry snapshot[LW_ENTITY_MAX_COOLDOWNS];
    int n_snapshot = self->n_cooldowns;
    for (int i = 0; i < n_snapshot; i++) snapshot[i] = self->m_cooldown[i];

    for (int i = 0; i < n_snapshot; i++) {
        int key = snapshot[i].key;
        int val = snapshot[i].value;
        if (val <= 1) {
            lw_iie_remove(self->m_cooldown, &self->n_cooldowns, key);
        } else {
            lw_iie_put(self->m_cooldown, &self->n_cooldowns,
                       LW_ENTITY_MAX_COOLDOWNS, key, val - 1);
        }
    }
}


/* ---- Chips / cooldowns / weapons / item uses ---------------------- */

/* Java: public void addChip(Chip chip) {
 *     if (chip != null) {
 *         if (mChips.size() < getRAM()) {
 *             mChips.put(chip.getId(), chip);
 *         }
 *     }
 * }
 *
 * Insert keeping ascending key order (TreeMap parity).
 */
void lw_entity_add_chip(LwEntity *self, struct LwChip *chip) {
    if (chip == NULL) return;
    if (self->n_chips >= lw_entity_get_ram(self)) return;
    if (self->n_chips >= LW_ENTITY_MAX_CHIPS) return;

    int chip_id = lw_chip_get_id(chip);
    /* Replace if already present */
    for (int i = 0; i < self->n_chips; i++) {
        if (self->m_chip_ids[i] == chip_id) {
            self->m_chips[i] = chip;
            return;
        }
    }
    /* Find insertion position (sorted by id ascending). */
    int pos = self->n_chips;
    for (int i = 0; i < self->n_chips; i++) {
        if (chip_id < self->m_chip_ids[i]) { pos = i; break; }
    }
    for (int i = self->n_chips; i > pos; i--) {
        self->m_chips[i] = self->m_chips[i - 1];
        self->m_chip_ids[i] = self->m_chip_ids[i - 1];
    }
    self->m_chips[pos] = chip;
    self->m_chip_ids[pos] = chip_id;
    self->n_chips++;
}

/* Java: public Chip getChip(int id) { return mChips.get(id); } */
struct LwChip* lw_entity_get_chip(const LwEntity *self, int id) {
    for (int i = 0; i < self->n_chips; i++) {
        if (self->m_chip_ids[i] == id) return self->m_chips[i];
    }
    return NULL;
}

int lw_entity_get_chips_count(const LwEntity *self) { return self->n_chips; }
struct LwChip* lw_entity_get_chip_at(const LwEntity *self, int index) {
    if (index < 0 || index >= self->n_chips) return NULL;
    return self->m_chips[index];
}

/* Java: public void addCooldown(Chip chip, int cooldown) {
 *     mCooldown.put(chip.getId(), cooldown == -1 ? State.MAX_TURNS + 2 : cooldown);
 * }
 */
void lw_entity_add_cooldown(LwEntity *self, struct LwChip *chip, int cooldown) {
    int v = (cooldown == -1) ? LW_ENTITY_MAX_TURNS_PLUS_2 : cooldown;
    lw_iie_put(self->m_cooldown, &self->n_cooldowns,
               LW_ENTITY_MAX_COOLDOWNS, lw_chip_get_id(chip), v);
}

/* Java: public boolean hasCooldown(int chipID) { return mCooldown.containsKey(chipID); } */
int lw_entity_has_cooldown(const LwEntity *self, int chip_id) {
    return lw_iie_find(self->m_cooldown, self->n_cooldowns, chip_id) >= 0 ? 1 : 0;
}

/* Java: public int getCooldown(int chipID) {
 *     if (!hasCooldown(chipID)) return 0;
 *     return mCooldown.get(chipID);
 * }
 */
int lw_entity_get_cooldown(const LwEntity *self, int chip_id) {
    int idx = lw_iie_find(self->m_cooldown, self->n_cooldowns, chip_id);
    if (idx < 0) return 0;
    return self->m_cooldown[idx].value;
}

int lw_entity_get_cooldowns_count(const LwEntity *self) { return self->n_cooldowns; }
int lw_entity_get_cooldown_key_at(const LwEntity *self, int index) {
    if (index < 0 || index >= self->n_cooldowns) return 0;
    return self->m_cooldown[index].key;
}
int lw_entity_get_cooldown_value_at(const LwEntity *self, int index) {
    if (index < 0 || index >= self->n_cooldowns) return 0;
    return self->m_cooldown[index].value;
}


/* Java: public long getItemUses(int itemID) {
 *     return this.itemUses.getOrDefault(itemID, 0);
 * }
 */
int lw_entity_get_item_uses(const LwEntity *self, int item_id) {
    int idx = lw_iie_find(self->item_uses, self->n_item_uses, item_id);
    if (idx < 0) return 0;
    return self->item_uses[idx].value;
}

/* Java: public void addItemUse(int id) {
 *     this.itemUses.merge(id, 1, Integer::sum);
 * }
 */
void lw_entity_add_item_use(LwEntity *self, int id) {
    int idx = lw_iie_find(self->item_uses, self->n_item_uses, id);
    if (idx >= 0) {
        self->item_uses[idx].value += 1;
    } else {
        lw_iie_put(self->item_uses, &self->n_item_uses,
                   LW_ENTITY_MAX_ITEM_USES, id, 1);
    }
}


/* Java: public void addWeapon(Weapon w) {
 *     mWeapons.add(w);
 *     passiveEffects.addAll(w.getPassiveEffects());
 * }
 */
void lw_entity_add_weapon(LwEntity *self, struct LwWeapon *w) {
    if (self->n_weapons >= LW_ENTITY_MAX_WEAPONS) return;
    self->m_weapons[self->n_weapons++] = w;
    int npe = lw_weapon_get_passive_effects_count(w);
    for (int i = 0; i < npe; i++) {
        if (self->n_passive_effects >= LW_ENTITY_MAX_PASSIVE_EFFECTS) return;
        self->passive_effects[self->n_passive_effects++] = &lw_weapon_get_passive_effects(w)[i];
    }
}

/* Java: public boolean hasWeapon(int id_tmp) */
int lw_entity_has_weapon(const LwEntity *self, int id) {
    for (int i = 0; i < self->n_weapons; i++) {
        if (lw_weapon_get_id(self->m_weapons[i]) == id) return 1;
    }
    return 0;
}

int lw_entity_get_weapons_count(const LwEntity *self) { return self->n_weapons; }
struct LwWeapon* lw_entity_get_weapon_at(const LwEntity *self, int index) {
    if (index < 0 || index >= self->n_weapons) return NULL;
    return self->m_weapons[index];
}

struct LwWeapon* lw_entity_get_weapon(const LwEntity *self) { return self->weapon; }
void             lw_entity_set_weapon(LwEntity *self, struct LwWeapon *w) { self->weapon = w; }


/* ---- Effects ------------------------------------------------------- */

/* Java: public boolean hasEffect(int attackID) {
 *     for (Effect target_effect : effects) {
 *         if (target_effect.getAttack() != null && target_effect.getAttack().getItemId() == attackID) return true;
 *     }
 *     return false;
 * }
 */
int lw_entity_has_effect(const LwEntity *self, int attack_id) {
    for (int i = 0; i < self->n_effects; i++) {
        struct LwAttack *a = lw_effect_get_attack(self->effects[i]);
        if (a != NULL && lw_attack_get_item_id(a) == attack_id) return 1;
    }
    return 0;
}

/* Java: public void addEffect(Effect effect) { effects.add(effect); } */
void lw_entity_add_effect(LwEntity *self, struct LwEffect *effect) {
    lw_effect_arr_add(self->effects, &self->n_effects, LW_ENTITY_MAX_EFFECTS, effect);
}

/* Java: public void removeEffect(Effect effect) {
 *     state.log(new ActionRemoveEffect(effect.getLogID()));
 *     effects.remove(effect);
 *     updateBuffStats();
 * }
 */
void lw_entity_remove_effect(LwEntity *self, struct LwEffect *effect) {
    lw_actions_log_remove_effect(lw_state_get_actions(self->state),
                                 lw_effect_get_log_id(effect));
    lw_effect_arr_remove(self->effects, &self->n_effects, effect);
    lw_entity_update_buff_stats(self);
}

/* Java: public void addLaunchedEffect(Effect effect) { launchedEffects.add(effect); } */
void lw_entity_add_launched_effect(LwEntity *self, struct LwEffect *effect) {
    lw_effect_arr_add(self->launched_effects, &self->n_launched_effects,
                      LW_ENTITY_MAX_LAUNCHED_EFFECTS, effect);
}

/* Java: public void removeLaunchedEffect(Effect effect) { launchedEffects.remove(effect); } */
void lw_entity_remove_launched_effect(LwEntity *self, struct LwEffect *effect) {
    lw_effect_arr_remove(self->launched_effects, &self->n_launched_effects, effect);
}

/* Java: public void updateEffect(Effect effect) {
 *     state.log(new ActionUpdateEffect(effect.getLogID(), effect.value));
 * }
 */
void lw_entity_update_effect(LwEntity *self, struct LwEffect *effect) {
    lw_actions_log_update_effect(lw_state_get_actions(self->state),
                                 lw_effect_get_log_id(effect),
                                 lw_effect_get_value(effect));
}

/* Java: public void clearEffects() {
 *     for (int i = 0; i < effects.size(); ++i) {
 *         Effect effect = effects.get(i);
 *         effect.getCaster().removeLaunchedEffect(effect);
 *         removeEffect(effect);
 *         i--;
 *     }
 *     effects.clear();
 * }
 *
 * NOTE: removeEffect already shrinks effects[] AND calls updateBuffStats,
 * so the i-- is correct because each iteration consumes effects[0]
 * effectively. (Java iterates with manual indexing; the i-- + removal
 * pattern means we always look at effects[0] after each removal.)
 */
void lw_entity_clear_effects(LwEntity *self) {
    for (int i = 0; i < self->n_effects; ++i) {
        struct LwEffect *effect = self->effects[i];
        LwEntity *caster = lw_effect_get_caster(effect);
        if (caster != NULL) {
            lw_entity_remove_launched_effect(caster, effect);
        }
        lw_entity_remove_effect(self, effect);
        i--;
    }
    /* effects.clear() -- defensive (loop above should leave it empty). */
    self->n_effects = 0;
}

/* Java: public void reduceEffects(double percent, Entity caster) {
 *     for (int i = 0; i < effects.size(); ++i) {
 *         var effect = effects.get(i);
 *         // Irreductible effect? skip
 *         if ((effect.getModifiers() & Effect.MODIFIER_IRREDUCTIBLE) != 0) continue;
 *
 *         effect.reduce(percent, caster);
 *         if (effect.value <= 0) {
 *             effect.getCaster().removeLaunchedEffect(effect);
 *             removeEffect(effects.get(i));
 *             i--;
 *         } else {
 *             updateEffect(effects.get(i));
 *         }
 *     }
 *     updateBuffStats();
 * }
 */
void lw_entity_reduce_effects(LwEntity *self, double percent, LwEntity *caster) {
    for (int i = 0; i < self->n_effects; ++i) {
        struct LwEffect *effect = self->effects[i];
        if ((lw_effect_get_modifiers(effect) & LW_MODIFIER_IRREDUCTIBLE) != 0) continue;

        lw_effect_reduce(effect, percent, caster);
        if (lw_effect_get_value(effect) <= 0) {
            LwEntity *ec = lw_effect_get_caster(effect);
            if (ec != NULL) lw_entity_remove_launched_effect(ec, effect);
            lw_entity_remove_effect(self, self->effects[i]);
            i--;
        } else {
            lw_entity_update_effect(self, self->effects[i]);
        }
    }
    lw_entity_update_buff_stats(self);
}

/* Java: public void reduceEffectsTotal(double percent, Entity caster) {
 *     for (int i = 0; i < effects.size(); ++i) {
 *         var effect = effects.get(i);
 *         effect.reduce(percent, caster);
 *         if (effect.value <= 0) {
 *             effect.getCaster().removeLaunchedEffect(effect);
 *             removeEffect(effect);
 *             i--;
 *         } else {
 *             updateEffect(effect);
 *         }
 *     }
 *     updateBuffStats();
 * }
 */
void lw_entity_reduce_effects_total(LwEntity *self, double percent, LwEntity *caster) {
    for (int i = 0; i < self->n_effects; ++i) {
        struct LwEffect *effect = self->effects[i];
        lw_effect_reduce(effect, percent, caster);
        if (lw_effect_get_value(effect) <= 0) {
            LwEntity *ec = lw_effect_get_caster(effect);
            if (ec != NULL) lw_entity_remove_launched_effect(ec, effect);
            lw_entity_remove_effect(self, effect);
            i--;
        } else {
            lw_entity_update_effect(self, effect);
        }
    }
    lw_entity_update_buff_stats(self);
}

/* Java: public void clearPoisons(Entity caster) {
 *     int poisonsRemoved = 0;
 *     for (int i = 0; i < effects.size(); ++i) {
 *         Effect effect = effects.get(i);
 *         if (effect instanceof EffectPoison) {
 *             effect.getCaster().removeLaunchedEffect(effect);
 *             removeEffect(effect);
 *             i--;
 *             poisonsRemoved += effect.getValue();
 *         }
 *     }
 *     state.statistics.antidote(this, caster, poisonsRemoved);
 * }
 */
void lw_entity_clear_poisons(LwEntity *self, LwEntity *caster) {
    int poisons_removed = 0;
    for (int i = 0; i < self->n_effects; ++i) {
        struct LwEffect *effect = self->effects[i];
        /* Java: effect instanceof EffectPoison */
        if (lw_effect_get_id(effect) == LW_EFFECT_TYPE_POISON) {
            LwEntity *ec = lw_effect_get_caster(effect);
            if (ec != NULL) lw_entity_remove_launched_effect(ec, effect);
            lw_entity_remove_effect(self, effect);
            i--;
            poisons_removed += lw_effect_get_value(effect);
        }
    }
    lw_state_statistics_antidote(self->state, self, caster, poisons_removed);
}

/* Java: public void removeShackles() {
 *     for (int i = 0; i < effects.size(); ++i) {
 *         Effect effect = effects.get(i);
 *         if (effect instanceof EffectShackleTP || effect instanceof EffectShackleMP
 *             || effect instanceof EffectShackleAgility || effect instanceof EffectShackleMagic
 *             || effect instanceof EffectShackleStrength || effect instanceof EffectShackleWisdom) {
 *             effect.getCaster().removeLaunchedEffect(effect);
 *             removeEffect(effect);
 *             i--;
 *         }
 *     }
 * }
 */
void lw_entity_remove_shackles(LwEntity *self) {
    for (int i = 0; i < self->n_effects; ++i) {
        struct LwEffect *effect = self->effects[i];
        /* Java: effect instanceof EffectShackle{TP,MP,Agility,Magic,Strength,Wisdom} */
        int eid = lw_effect_get_id(effect);
        if (eid == LW_EFFECT_TYPE_SHACKLE_TP
                || eid == LW_EFFECT_TYPE_SHACKLE_MP
                || eid == LW_EFFECT_TYPE_SHACKLE_AGILITY
                || eid == LW_EFFECT_TYPE_SHACKLE_MAGIC
                || eid == LW_EFFECT_TYPE_SHACKLE_STRENGTH
                || eid == LW_EFFECT_TYPE_SHACKLE_WISDOM) {
            LwEntity *ec = lw_effect_get_caster(effect);
            if (ec != NULL) lw_entity_remove_launched_effect(ec, effect);
            lw_entity_remove_effect(self, effect);
            i--;
        }
    }
}

/* Iteration helpers (Java getEffects() / getLaunchedEffects() / getPassiveEffects()) */

int               lw_entity_get_effects_count       (const LwEntity *self) { return self->n_effects; }
struct LwEffect*  lw_entity_get_effect_at           (const LwEntity *self, int index) {
    if (index < 0 || index >= self->n_effects) return NULL;
    return self->effects[index];
}
int               lw_entity_get_launched_effects_count(const LwEntity *self) { return self->n_launched_effects; }
struct LwEffect*  lw_entity_get_launched_effect_at  (const LwEntity *self, int index) {
    if (index < 0 || index >= self->n_launched_effects) return NULL;
    return self->launched_effects[index];
}
int               lw_entity_get_passive_effects_count(const LwEntity *self) { return self->n_passive_effects; }
const LwEffectParameters* lw_entity_get_passive_effect_at(const LwEntity *self, int index) {
    if (index < 0 || index >= self->n_passive_effects) return NULL;
    return self->passive_effects[index];
}


/* ---- States -------------------------------------------------------- */

/* Java: public boolean hasState(EntityState state) {
 *     return this.states.contains(state);
 * }
 */
int lw_entity_has_state(const LwEntity *self, int state) {
    if (state < 0 || state >= 32) return 0;
    return (self->states & (1u << state)) != 0 ? 1 : 0;
}

/* Java: public void addState(EntityState state) {
 *     this.states.add(state);
 * }
 */
void lw_entity_add_state(LwEntity *self, int state) {
    if (state < 0 || state >= 32) return;
    self->states |= (1u << state);
}

LwEntityStateMask lw_entity_get_states(const LwEntity *self) { return self->states; }


/* ---- Resource usage ------------------------------------------------ */

/* Java: public void useTP(int tp) {
 *     usedTP += tp;
 *     state.statistics.useTP(tp);
 * }
 */
void lw_entity_use_tp(LwEntity *self, int tp) {
    self->used_tp += tp;
    lw_state_statistics_use_tp(self->state, tp);
}

/* Java: public void useMP(int mp) {
 *     usedMP += mp;
 *     state.statistics.useTP(mp);  // <- Java actually calls useTP here (verbatim)
 * }
 */
void lw_entity_use_mp(LwEntity *self, int mp) {
    self->used_mp += mp;
    lw_state_statistics_use_tp(self->state, mp);
}


/* ---- Misc bookkeeping --------------------------------------------- */

/* Java: public void resurrect(Entity entity, double factor, boolean fullLife) {
 *     if (fullLife) {
 *         life = mTotalLife;
 *     } else {
 *         mTotalLife = Math.max(10, (int) Math.round(mTotalLife * 0.5 * factor));
 *         life = mTotalLife / 2;
 *     }
 *     resurrected++;
 *     endTurn();
 * }
 */
void lw_entity_resurrect(LwEntity *self, LwEntity *entity, double factor, int full_life) {
    (void)entity;  /* parameter present in Java signature but unused inside */
    if (full_life) {
        self->life = self->m_total_life;
    } else {
        int rounded = lw_java_round((double)self->m_total_life * 0.5 * factor);
        self->m_total_life = lw_max_int(10, rounded);
        self->life = self->m_total_life / 2;
    }
    self->resurrected++;
    lw_entity_end_turn(self);
}

int     lw_entity_get_resurrected     (const LwEntity *self) { return self->resurrected; }
int64_t lw_entity_get_total_operations(const LwEntity *self) { return self->total_operations; }
void    lw_entity_add_operations      (LwEntity *self, int64_t operations) {
    self->total_operations += operations;
}

int  lw_entity_get_birth_turn(const LwEntity *self) { return self->m_birth_turn; }
void lw_entity_set_birth_turn(LwEntity *self, int turn) { self->m_birth_turn = turn; }


/* Java: public int getDistance(Entity entity) {
 *     if (isDead() || entity.isDead()) return 999;
 *     return Pathfinding.getCaseDistance(getCell(), entity.getCell());
 * }
 */
int lw_entity_get_distance(const LwEntity *self, const LwEntity *entity) {
    if (lw_entity_is_dead(self) || lw_entity_is_dead(entity)) return 999;
    return lw_pathfinding_get_case_distance(self->cell, entity->cell);
}


/* Java: public List<Entity> getEntitiesAround(int distance) {
 *     List<Entity> entities = new ArrayList<Entity>();
 *     for (Entity entity : state.getEntities().values()) {
 *         if (entity != this && entity.getDistance(this) <= distance) {
 *             entities.add(entity);
 *         }
 *     }
 *     return entities;
 * }
 */
int lw_entity_get_entities_around(LwEntity *self, int distance,
                                  LwEntity **out_buf, int out_cap) {
    int n = 0;
    LwEntity **all = lw_state_get_entities(self->state, &n);
    int written = 0;
    for (int i = 0; i < n && written < out_cap; i++) {
        LwEntity *entity = all[i];
        if (entity != self && lw_entity_get_distance(entity, self) <= distance) {
            out_buf[written++] = entity;
        }
    }
    return written;
}


/* Java: public List<Entity> getSummons(boolean get_dead) {
 *     List<Entity> summons = new ArrayList<Entity>();
 *     for (Entity e : state.getTeamEntities(getTeam(), get_dead)) {
 *         if (e.isSummon() && e.getSummoner().getFId() == getFId()) {
 *             summons.add(e);
 *         }
 *     }
 *     return summons;
 * }
 */
int lw_entity_get_summons(LwEntity *self, int get_dead,
                          LwEntity **out_buf, int out_cap) {
    LwEntity *team_entities[64];
    int n = lw_state_get_team_entities_ex(self->state, lw_entity_get_team(self),
                                              get_dead, team_entities, 64);
    int written = 0;
    for (int i = 0; i < n && written < out_cap; i++) {
        LwEntity *e = team_entities[i];
        LwEntity *sm = lw_entity_get_summoner(e);
        if (lw_entity_is_summon(e) && sm != NULL && lw_entity_get_fid(sm) == lw_entity_get_fid(self)) {
            out_buf[written++] = e;
        }
    }
    return written;
}


/* ---- State / AI / logs / fight pointers --------------------------- */

/* Java: public void setState(State state, int fid) {
 *     this.state = state;
 *     this.fight_id = fid;
 * }
 */
void lw_entity_set_state(LwEntity *self, struct LwState *state, int fid) {
    self->state = state;
    self->fight_id = fid;
}

void* lw_entity_get_ai      (const LwEntity *self) { return self->ai; }
void  lw_entity_set_ai      (LwEntity *self, void *ai) { self->ai = ai; }
void* lw_entity_get_logs    (const LwEntity *self) { return self->logs; }
void  lw_entity_set_logs    (LwEntity *self, void *logs) { self->logs = logs; }
void* lw_entity_get_fight   (const LwEntity *self) { return self->fight; }
void  lw_entity_set_fight   (LwEntity *self, void *fight) { self->fight = fight; }
void* lw_entity_get_ai_file (const LwEntity *self) { return self->ai_file; }
void  lw_entity_set_ai_file (LwEntity *self, void *ai_file) { self->ai_file = ai_file; }

struct LwRegisters* lw_entity_get_registers(const LwEntity *self) { return self->m_register; }
void                lw_entity_set_registers(LwEntity *self, struct LwRegisters *r) {
    self->m_register = r;
}


/* ---- toString ------------------------------------------------------ */

/* Java: public String toString() { return name; } */
const char* lw_entity_to_string(const LwEntity *self) { return self->name; }
