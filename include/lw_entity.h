/*
 * lw_entity.h -- 1:1 port of state/Entity.java (the abstract base) with
 * subtype-specific behaviour folded into the same struct.
 *
 * Java has Entity as an abstract base class with concrete subclasses:
 *   - leek/Leek      (TYPE_LEEK)
 *   - entity/Bulb    (TYPE_BULB)
 *   - turret/Turret  (TYPE_TURRET)
 *   - chest/...      (TYPE_CHEST)
 *   - mob/...        (TYPE_MOB)
 *
 * In C we use a single LwEntity struct with a `type` field; the few
 * subclass-specific overrides (getType, isSummon, getSummoner, startFight,
 * getLeek) are dispatched through helper functions in lw_entity.c.
 *
 * The Leek-specific extras (skin/hat/face/metal) are stored on the base
 * struct because Java's Entity already exposes them as fields with
 * setters/getters. The Bulb-specific summoner pointer is also stored
 * here, matching Java where Entity.getSummoner returns null and Bulb
 * overrides it to return mOwner.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/Entity.java
 *            java_reference/src/main/java/com/leekwars/generator/leek/Leek.java
 *            java_reference/src/main/java/com/leekwars/generator/entity/Bulb.java
 *            java_reference/src/main/java/com/leekwars/generator/turret/Turret.java
 */
#ifndef LW_ENTITY_H
#define LW_ENTITY_H

#include <stdint.h>

#include "lw_constants.h"
#include "lw_stats.h"
#include "lw_cell.h"
#include "lw_effect_params.h"   /* LwEffectParameters is an anonymous-struct typedef. */


/* Forward decls -- the structs/functions used inside Entity that live in
 * other modules. The C file resolves them via .c includes; consumers of
 * this header only need the pointer types. */
struct LwState;
struct LwEffect;
struct LwActions;
struct LwAttack;
struct LwItem;
struct LwTeam;
struct LwWeapon;
struct LwChip;
struct LwRegisters;


/* ---- Capacities ----------------------------------------------------- *
 *
 * Java uses ArrayList / HashMap with no fixed cap. We use bounded arrays
 * that comfortably fit any observed fight; sizes derived from the largest
 * fight states the upstream Python harness has produced.
 */

#define LW_ENTITY_MAX_EFFECTS          64   /* effects[] -- effects ON this entity */
#define LW_ENTITY_MAX_LAUNCHED_EFFECTS 64   /* launchedEffects[] */
#define LW_ENTITY_MAX_PASSIVE_EFFECTS  16   /* passiveEffects[] */
#define LW_ENTITY_MAX_WEAPONS          18   /* mWeapons[] */
#define LW_ENTITY_MAX_CHIPS            32   /* mChips (TreeMap<Integer,Chip>) */
#define LW_ENTITY_MAX_COOLDOWNS        32   /* mCooldown (TreeMap<Integer,Integer>) */
#define LW_ENTITY_MAX_ITEM_USES        32   /* itemUses (HashMap<Integer,Integer>) */
#define LW_ENTITY_NAME_MAX             64
#define LW_ENTITY_FARMER_NAME_MAX      64
#define LW_ENTITY_FARMER_COUNTRY_MAX   8
#define LW_ENTITY_TEAM_NAME_MAX        64
#define LW_ENTITY_COMP_NAME_MAX        64
#define LW_ENTITY_AI_NAME_MAX          64


/* TreeMap<Integer,Integer> entry -- used for cooldowns and itemUses.
 *
 * NOTE: TreeMap iteration order is by key ascending. We keep the array
 * sorted on insert so a linear walk yields the same order Java's
 * entrySet() would. */
typedef struct {
    int key;
    int value;
    int present;   /* 0 = empty slot */
} LwIntIntEntry;


/* Java enum EntityState bitfield. We track presence as a bitmask over
 * LW_ENTITY_STATE_*; addState sets the bit, hasState tests it,
 * updateBuffStats clears it before re-folding the effect list. */
typedef uint32_t LwEntityStateMask;


/* ---- Main struct --------------------------------------------------- */

typedef struct LwEntity {

    /* ---- subtype tag (Java: getType() override) -------------------- */
    int                 type;                /* LW_ENTITY_TYPE_* */

    /* ---- Identity (Java fields) ------------------------------------ */
    int                 m_id;                /* private int mId */
    int                 fight_id;            /* private int fight_id */
    int                 m_farmer;            /* private int mFarmer */
    int                 m_level;             /* private int mLevel */

    char                name[LW_ENTITY_NAME_MAX];                       /* protected String name */
    char                m_farmer_name[LW_ENTITY_FARMER_NAME_MAX];       /* protected String mFarmerName = "" */
    char                m_farmer_country[LW_ENTITY_FARMER_COUNTRY_MAX]; /* protected String mFarmerCountry */
    int                 m_farmer_country_set;                           /* 0 = null in Java */
    char                m_team_name[LW_ENTITY_TEAM_NAME_MAX];           /* protected String mTeamName = "" */
    char                m_composition_name[LW_ENTITY_COMP_NAME_MAX];    /* protected String mCompositionName = null */
    int                 m_composition_name_set;                         /* 0 = null in Java */
    char                m_ai_name[LW_ENTITY_AI_NAME_MAX];               /* protected String mAIName */

    int                 m_team_id;           /* protected int mTeamId */
    int                 m_ai_id;             /* protected int mAIId */

    /* Leek-specific cosmetics (also defined on Entity in Java) */
    int                 m_skin;              /* protected int mSkin */
    int                 m_hat;               /* protected int mHat */
    int                 m_metal;             /* protected boolean mMetal */
    int                 m_face;              /* protected int mFace */

    /* ---- Combat stats ---------------------------------------------- */
    LwCell             *cell;                /* protected Cell cell */
    LwStats             m_base_stats;        /* protected final Stats mBaseStats */
    LwStats             m_buff_stats;        /* protected final Stats mBuffStats */

    int                 life;                /* private int life */
    int                 m_total_life;        /* protected int mTotalLife */
    int                 m_initial_life;      /* protected int mInitialLife */

    int                 used_tp;             /* private int usedTP */
    int                 used_mp;             /* private int usedMP */

    /* ---- Effects --------------------------------------------------- */
    /* protected final ArrayList<Effect> effects = new ArrayList<>();
     * NOTE: we store pointers to effects owned by LwState. */
    struct LwEffect    *effects[LW_ENTITY_MAX_EFFECTS];
    int                 n_effects;

    /* private final ArrayList<Effect> launchedEffects = new ArrayList<>(); */
    struct LwEffect    *launched_effects[LW_ENTITY_MAX_LAUNCHED_EFFECTS];
    int                 n_launched_effects;

    /* private ArrayList<EffectParameters> passiveEffects = new ArrayList<>();
     * NOTE: pointers into immutable Item/Weapon descriptors. */
    const LwEffectParameters *passive_effects[LW_ENTITY_MAX_PASSIVE_EFFECTS];
    int                 n_passive_effects;

    /* ---- Cooldowns / item uses ------------------------------------- */
    /* protected Map<Integer,Integer> mCooldown = new TreeMap<>(); */
    LwIntIntEntry       m_cooldown[LW_ENTITY_MAX_COOLDOWNS];
    int                 n_cooldowns;

    /* private HashMap<Integer,Integer> itemUses = new HashMap<>(); */
    LwIntIntEntry       item_uses[LW_ENTITY_MAX_ITEM_USES];
    int                 n_item_uses;

    /* ---- States (HashSet<EntityState>) ----------------------------- */
    LwEntityStateMask   states;              /* private Set<EntityState> states */

    /* ---- Team / loadout ------------------------------------------- */
    int                 team;                /* protected int team */

    /* private TreeMap<Integer,Chip> mChips */
    struct LwChip      *m_chips[LW_ENTITY_MAX_CHIPS];
    int                 m_chip_ids[LW_ENTITY_MAX_CHIPS];
    int                 n_chips;

    /* private List<Weapon> mWeapons + private Weapon weapon */
    struct LwWeapon    *m_weapons[LW_ENTITY_MAX_WEAPONS];
    int                 n_weapons;
    struct LwWeapon    *weapon;

    /* ---- Per-turn / fight bookkeeping ------------------------------ */
    int                 m_static;            /* protected boolean mStatic */
    int                 resurrected;         /* protected int resurrected = 0 */
    int64_t             total_operations;    /* protected long totalOperations = 0 */
    int                 says_turn;           /* public int saysTurn = 0 */
    int                 shows_turn;          /* public int showsTurn = 0 */
    int                 m_birth_turn;        /* protected int mBirthTurn = 1 */

    int                 m_has_moved;         /* private boolean mHasMoved = false */

    int                 initial_cell;        /* private Integer initialCell = null  (-1 = null) */
    int                 initial_cell_set;    /* 0 = null in Java */
    int                 orientation;         /* private int orientation = -1 */

    /* ---- Subtype-specific extras ----------------------------------- */
    /* Bulb.mOwner -- only meaningful for TYPE_BULB. NULL otherwise. */
    struct LwEntity    *m_owner;

    /* ---- State / opaque pointers ----------------------------------- */
    struct LwState     *state;               /* public State state */
    struct LwRegisters *m_register;          /* private Registers mRegister = null */

    void               *ai;                  /* private Object ai */
    void               *logs;                /* private Object logs */
    void               *fight;               /* private Object fight */
    void               *ai_file;             /* private Object aiFile */

} LwEntity;


/* ---- Constructors -------------------------------------------------- *
 *
 * Java: public Entity() { this(0, ""); }
 * Java: public Entity(Integer id, String name) { ... }
 * Java: public Entity(Integer id, String name, int farmer, ...) { ... full ctor ... }
 * Java: public Entity(Entity entity) { ... copy ctor ... }
 *
 * In C we keep three init flavours mirroring the Java constructors. The
 * subtype-specific construction (Leek/Bulb/Turret) sets `type` and any
 * extras after calling the base init. */

void lw_entity_init_default(LwEntity *self);

void lw_entity_init_id_name(LwEntity *self, int id, const char *name);

void lw_entity_init_full(LwEntity *self, int id, const char *name, int farmer,
                         int level, int life, int turn_point, int move_point,
                         int force, int agility, int frequency, int wisdom,
                         int resistance, int science, int magic, int cores, int ram,
                         int skin, int metal, int face, int team_id,
                         const char *team_name, int ai_id, const char *ai_name,
                         const char *farmer_name, const char *farmer_country, int hat);

void lw_entity_init_copy(LwEntity *self, const LwEntity *src);


/* ---- Type / identity getters ---------------------------------------- */

/* Java: public abstract int getType(); */
int  lw_entity_get_type     (const LwEntity *self);

/* Java: public Leek getLeek() { return null; } / Leek overrides to return this. */
LwEntity* lw_entity_get_leek(LwEntity *self);

/* Java: public boolean isSummon() { return false; } / Bulb overrides to true. */
int  lw_entity_is_summon    (const LwEntity *self);

/* Java: public Entity getSummoner() { return null; } / Bulb overrides to mOwner. */
LwEntity* lw_entity_get_summoner(const LwEntity *self);

/* Java: public int getFId() */
int  lw_entity_get_fid      (const LwEntity *self);

/* Java: public int getId() */
int  lw_entity_get_id       (const LwEntity *self);

/* Java: public void setId(int id) */
void lw_entity_set_id       (LwEntity *self, int id);

/* Java: public int getLevel() */
int  lw_entity_get_level    (const LwEntity *self);

/* Java: public void setLevel(int level) */
void lw_entity_set_level    (LwEntity *self, int level);

/* Java: public int getFarmer() */
int  lw_entity_get_farmer   (const LwEntity *self);

/* Java: public void setFarmer(int farmer) */
void lw_entity_set_farmer   (LwEntity *self, int farmer);

/* Java: public String getName() / setName() */
const char* lw_entity_get_name(const LwEntity *self);
void        lw_entity_set_name(LwEntity *self, const char *name);

/* Java: public int getTeam() / setTeam() */
int  lw_entity_get_team     (const LwEntity *self);
void lw_entity_set_team     (LwEntity *self, int team);

/* Java: public int getTeamId() / setTeamID() */
int  lw_entity_get_team_id  (const LwEntity *self);
void lw_entity_set_team_id  (LwEntity *self, int team);

/* Java: public String getTeamName() / setTeamName() */
const char* lw_entity_get_team_name(const LwEntity *self);
void        lw_entity_set_team_name(LwEntity *self, const char *name);

/* Java: public String getCompositionName() / setCompositionName() */
const char* lw_entity_get_composition_name(const LwEntity *self);
void        lw_entity_set_composition_name(LwEntity *self, const char *name);

/* Java: public String getAIName() / setAIName() */
const char* lw_entity_get_ai_name(const LwEntity *self);
void        lw_entity_set_ai_name(LwEntity *self, const char *ai);

/* Java: public int getAIId() */
int  lw_entity_get_ai_id    (const LwEntity *self);

/* Java: public String getFarmerName() / setFarmerName() */
const char* lw_entity_get_farmer_name(const LwEntity *self);
void        lw_entity_set_farmer_name(LwEntity *self, const char *name);

/* Java: public String getFarmerCountry() / setFarmerCountry() */
const char* lw_entity_get_farmer_country(const LwEntity *self);
void        lw_entity_set_farmer_country(LwEntity *self, const char *country);

/* Java: public int getHat() / setHat() / getSkin() / setSkin() / getMetal() / setMetal() / getFace() / setFace() */
int  lw_entity_get_hat      (const LwEntity *self);
void lw_entity_set_hat      (LwEntity *self, int hat);
int  lw_entity_get_skin     (const LwEntity *self);
void lw_entity_set_skin     (LwEntity *self, int skin);
int  lw_entity_get_metal    (const LwEntity *self);
void lw_entity_set_metal    (LwEntity *self, int metal);
int  lw_entity_get_face     (const LwEntity *self);
void lw_entity_set_face     (LwEntity *self, int face);


/* ---- Cell / orientation -------------------------------------------- */

/* Java: public Cell getCell() / setCell() */
LwCell* lw_entity_get_cell  (const LwEntity *self);
void    lw_entity_set_cell  (LwEntity *self, LwCell *cell);

/* Java: public Integer getInitialCell() / setInitialCell() */
int  lw_entity_get_initial_cell(const LwEntity *self);
void lw_entity_set_initial_cell(LwEntity *self, int cell);
int  lw_entity_has_initial_cell(const LwEntity *self);

/* Java: public int getOrientation() / setOrientation() */
int  lw_entity_get_orientation(const LwEntity *self);
void lw_entity_set_orientation(LwEntity *self, int orientation);


/* ---- Stats ---------------------------------------------------------- */

/* Java: public Stats getBaseStats() */
LwStats* lw_entity_get_base_stats(LwEntity *self);

/* Java: public int getStat(int id) -- combines base + buff */
int  lw_entity_get_stat       (const LwEntity *self, int id);

/* Java: per-stat getters */
int  lw_entity_get_strength       (const LwEntity *self);
int  lw_entity_get_agility        (const LwEntity *self);
int  lw_entity_get_resistance     (const LwEntity *self);
int  lw_entity_get_science        (const LwEntity *self);
int  lw_entity_get_magic          (const LwEntity *self);
int  lw_entity_get_wisdom         (const LwEntity *self);
int  lw_entity_get_relative_shield(const LwEntity *self);
int  lw_entity_get_absolute_shield(const LwEntity *self);
int  lw_entity_get_damage_return  (const LwEntity *self);
int  lw_entity_get_frequency      (const LwEntity *self);
int  lw_entity_get_cores          (const LwEntity *self);
int  lw_entity_get_ram            (const LwEntity *self);
int  lw_entity_get_total_tp       (const LwEntity *self);
int  lw_entity_get_total_mp       (const LwEntity *self);
int  lw_entity_get_mp             (const LwEntity *self);
int  lw_entity_get_tp             (const LwEntity *self);
int  lw_entity_get_power          (const LwEntity *self);

/* Java: per-stat setters */
void lw_entity_set_life       (LwEntity *self, int life);
void lw_entity_set_strength   (LwEntity *self, int strength);
void lw_entity_set_agility    (LwEntity *self, int agility);
void lw_entity_set_wisdom     (LwEntity *self, int wisdom);
void lw_entity_set_resistance (LwEntity *self, int resistance);
void lw_entity_set_science    (LwEntity *self, int science);
void lw_entity_set_magic      (LwEntity *self, int magic);
void lw_entity_set_frequency  (LwEntity *self, int frequency);
void lw_entity_set_cores      (LwEntity *self, int cores);
void lw_entity_set_ram        (LwEntity *self, int ram);
void lw_entity_set_tp         (LwEntity *self, int tp);
void lw_entity_set_mp         (LwEntity *self, int mp);
void lw_entity_set_relative_shield(LwEntity *self, int shield);
void lw_entity_set_absolute_shield(LwEntity *self, int shield);


/* ---- Buff stats ---------------------------------------------------- */

/* Java: public void updateBuffStats() -- recomputes from effects[] */
void lw_entity_update_buff_stats     (LwEntity *self);

/* Java: public void updateBuffStats(int id, int delta, Entity caster) */
void lw_entity_update_buff_stats_with(LwEntity *self, int id, int delta, LwEntity *caster);


/* ---- Life / damage ------------------------------------------------- */

/* Java: public int getLife() */
int  lw_entity_get_life       (const LwEntity *self);

/* Java: public int getTotalLife() */
int  lw_entity_get_total_life (const LwEntity *self);

/* Java: public int getInitialLife() */
int  lw_entity_get_initial_life(const LwEntity *self);

/* Java: public void addTotalLife(int vitality, Entity caster) */
void lw_entity_add_total_life (LwEntity *self, int vitality, LwEntity *caster);

/* Java: public void setTotalLife(int vitality) */
void lw_entity_set_total_life (LwEntity *self, int vitality);

/* Java: public boolean isDead() / isAlive() */
int  lw_entity_is_dead        (const LwEntity *self);
int  lw_entity_is_alive       (const LwEntity *self);

/* Java: public void setDead(boolean dead) */
void lw_entity_set_dead       (LwEntity *self, int dead);

/* Java: public void removeLife(int pv, int erosion, Entity attacker, DamageType type, Effect effect, Item item) */
void lw_entity_remove_life    (LwEntity *self, int pv, int erosion,
                               LwEntity *attacker, int damage_type,
                               struct LwEffect *effect, struct LwItem *item);

/* Java: public void addLife(Entity healer, int pv) */
void lw_entity_add_life       (LwEntity *self, LwEntity *healer, int pv);

/* Java: public void die() */
void lw_entity_die            (LwEntity *self);


/* ---- Passive damage hooks ------------------------------------------ */

/* Java: public void onDirectDamage(int damage) */
void lw_entity_on_direct_damage(LwEntity *self, int damage);

/* Java: public void onNovaDamage(int damage) */
void lw_entity_on_nova_damage  (LwEntity *self, int damage);

/* Java: public void onPoisonDamage(int damage) */
void lw_entity_on_poison_damage(LwEntity *self, int damage);

/* Java: public void onMoved(Entity by) */
void lw_entity_on_moved        (LwEntity *self, LwEntity *by);

/* Java: public void onAllyKilled() */
void lw_entity_on_ally_killed  (LwEntity *self);

/* Java: public void onCritical() */
void lw_entity_on_critical     (LwEntity *self);

/* Java: public void onKill() */
void lw_entity_on_kill         (LwEntity *self);

/* Java: passive effect activator helpers (called from on*Damage) */
void lw_entity_activate_on_moved_passive_effect       (LwEntity *self, const LwEffectParameters *effect, struct LwAttack *attack);
void lw_entity_activate_on_damage_passive_effect      (LwEntity *self, const LwEffectParameters *effect, struct LwAttack *attack, int input_value);
void lw_entity_activate_on_nova_damage_passive_effect (LwEntity *self, const LwEffectParameters *effect, struct LwAttack *attack, int input_value);
void lw_entity_activate_on_poison_damage_passive_effect(LwEntity *self, const LwEffectParameters *effect, struct LwAttack *attack, int input_value);
void lw_entity_activate_on_ally_killed_passive_effect (LwEntity *self, const LwEffectParameters *effect, struct LwAttack *attack);
void lw_entity_activate_on_critical_passive_effect    (LwEntity *self, const LwEffectParameters *effect, struct LwAttack *attack);
void lw_entity_activate_on_kill_passive_effect        (LwEntity *self, const LwEffectParameters *effect, struct LwAttack *attack);


/* ---- Turn lifecycle ------------------------------------------------ */

/* Java: public void startTurn() */
void lw_entity_start_turn     (LwEntity *self);

/* Java: public void endTurn() */
void lw_entity_end_turn       (LwEntity *self);

/* Java: public void startFight() */
void lw_entity_start_fight    (LwEntity *self);

/* Java: public void applyCoolDown() */
void lw_entity_apply_cool_down(LwEntity *self);


/* ---- Chips / cooldowns / weapons / item uses ------------------------ */

/* Java: public void addChip(Chip chip) */
void lw_entity_add_chip       (LwEntity *self, struct LwChip *chip);

/* Java: public Chip getChip(int id) */
struct LwChip* lw_entity_get_chip(const LwEntity *self, int id);

/* Java: public List<Chip> getChips() -- returns count + writes to caller buf */
int  lw_entity_get_chips_count(const LwEntity *self);
struct LwChip* lw_entity_get_chip_at(const LwEntity *self, int index);

/* Java: public void addCooldown(Chip chip, int cooldown) */
void lw_entity_add_cooldown   (LwEntity *self, struct LwChip *chip, int cooldown);

/* Java: public boolean hasCooldown(int chipID) */
int  lw_entity_has_cooldown   (const LwEntity *self, int chip_id);

/* Java: public int getCooldown(int chipID) */
int  lw_entity_get_cooldown   (const LwEntity *self, int chip_id);

/* Java: public Map<Integer,Integer> getCooldowns() -- iteration helpers */
int  lw_entity_get_cooldowns_count(const LwEntity *self);
int  lw_entity_get_cooldown_key_at(const LwEntity *self, int index);
int  lw_entity_get_cooldown_value_at(const LwEntity *self, int index);

/* Java: public long getItemUses(int itemID) */
int  lw_entity_get_item_uses  (const LwEntity *self, int item_id);

/* Java: public void addItemUse(int id) */
void lw_entity_add_item_use   (LwEntity *self, int id);

/* Java: public void addWeapon(Weapon w) */
void lw_entity_add_weapon     (LwEntity *self, struct LwWeapon *w);

/* Java: public boolean hasWeapon(int id_tmp) */
int  lw_entity_has_weapon     (const LwEntity *self, int id);

/* Java: public List<Weapon> getWeapons() / getWeapon() / setWeapon() */
int  lw_entity_get_weapons_count(const LwEntity *self);
struct LwWeapon* lw_entity_get_weapon_at(const LwEntity *self, int index);
struct LwWeapon* lw_entity_get_weapon   (const LwEntity *self);
void             lw_entity_set_weapon   (LwEntity *self, struct LwWeapon *w);


/* ---- Effects ------------------------------------------------------- */

/* Java: public boolean hasEffect(int attackID) */
int  lw_entity_has_effect            (const LwEntity *self, int attack_id);

/* Java: public void addEffect(Effect effect) */
void lw_entity_add_effect            (LwEntity *self, struct LwEffect *effect);

/* Java: public void removeEffect(Effect effect) */
void lw_entity_remove_effect         (LwEntity *self, struct LwEffect *effect);

/* Java: public void addLaunchedEffect(Effect effect) */
void lw_entity_add_launched_effect   (LwEntity *self, struct LwEffect *effect);

/* Java: public void removeLaunchedEffect(Effect effect) */
void lw_entity_remove_launched_effect(LwEntity *self, struct LwEffect *effect);

/* Java: public void updateEffect(Effect effect) */
void lw_entity_update_effect         (LwEntity *self, struct LwEffect *effect);

/* Java: public void clearEffects() */
void lw_entity_clear_effects         (LwEntity *self);

/* Java: public void reduceEffects(double percent, Entity caster) */
void lw_entity_reduce_effects        (LwEntity *self, double percent, LwEntity *caster);

/* Java: public void reduceEffectsTotal(double percent, Entity caster) */
void lw_entity_reduce_effects_total  (LwEntity *self, double percent, LwEntity *caster);

/* Java: public void clearPoisons(Entity caster) */
void lw_entity_clear_poisons         (LwEntity *self, LwEntity *caster);

/* Java: public void removeShackles() */
void lw_entity_remove_shackles       (LwEntity *self);

/* Iteration helpers (Java: public List<Effect> getEffects() / getLaunchedEffects() / getPassiveEffects()) */
int                  lw_entity_get_effects_count          (const LwEntity *self);
struct LwEffect*     lw_entity_get_effect_at              (const LwEntity *self, int index);
int                  lw_entity_get_launched_effects_count (const LwEntity *self);
struct LwEffect*     lw_entity_get_launched_effect_at     (const LwEntity *self, int index);
int                  lw_entity_get_passive_effects_count  (const LwEntity *self);
const LwEffectParameters* lw_entity_get_passive_effect_at(const LwEntity *self, int index);


/* ---- States -------------------------------------------------------- */

/* Java: public boolean hasState(EntityState state) */
int  lw_entity_has_state      (const LwEntity *self, int state);

/* Java: public void addState(EntityState state) */
void lw_entity_add_state      (LwEntity *self, int state);

/* Java: public Set<EntityState> getStates() -- bitmask access */
LwEntityStateMask lw_entity_get_states(const LwEntity *self);


/* ---- Resource usage ------------------------------------------------ */

/* Java: public void useTP(int tp) */
void lw_entity_use_tp         (LwEntity *self, int tp);

/* Java: public void useMP(int mp) */
void lw_entity_use_mp         (LwEntity *self, int mp);


/* ---- Misc bookkeeping ---------------------------------------------- */

/* Java: public void resurrect(Entity entity, double factor, boolean fullLife) */
void lw_entity_resurrect      (LwEntity *self, LwEntity *entity, double factor, int full_life);

/* Java: public int getResurrected() */
int  lw_entity_get_resurrected(const LwEntity *self);

/* Java: public long getTotalOperations() / addOperations(long) */
int64_t lw_entity_get_total_operations(const LwEntity *self);
void    lw_entity_add_operations      (LwEntity *self, int64_t operations);

/* Java: public int getBirthTurn() / setBirthTurn() */
int  lw_entity_get_birth_turn (const LwEntity *self);
void lw_entity_set_birth_turn (LwEntity *self, int turn);

/* Java: public int getDistance(Entity entity) */
int  lw_entity_get_distance   (const LwEntity *self, const LwEntity *entity);

/* Java: public List<Entity> getEntitiesAround(int distance)
 * Writes into caller buffer; returns count written. */
int  lw_entity_get_entities_around(LwEntity *self, int distance,
                                   LwEntity **out_buf, int out_cap);

/* Java: public List<Entity> getSummons(boolean get_dead) */
int  lw_entity_get_summons    (LwEntity *self, int get_dead,
                               LwEntity **out_buf, int out_cap);


/* ---- State / AI / logs / fight pointers ---------------------------- */

/* Java: public void setState(State state, int fid) */
void lw_entity_set_state      (LwEntity *self, struct LwState *state, int fid);

/* Java: public Object getAI() / setAI() */
void* lw_entity_get_ai        (const LwEntity *self);
void  lw_entity_set_ai        (LwEntity *self, void *ai);

/* Java: public Object getLogs() / setLogs() */
void* lw_entity_get_logs      (const LwEntity *self);
void  lw_entity_set_logs      (LwEntity *self, void *logs);

/* Java: public Object getFight() / setFight() */
void* lw_entity_get_fight     (const LwEntity *self);
void  lw_entity_set_fight     (LwEntity *self, void *fight);

/* Java: public Object getAIFile() / setAIFile() */
void* lw_entity_get_ai_file   (const LwEntity *self);
void  lw_entity_set_ai_file   (LwEntity *self, void *ai_file);

/* Java: public Registers getRegisters() / setRegisters() */
struct LwRegisters* lw_entity_get_registers(const LwEntity *self);
void                lw_entity_set_registers(LwEntity *self, struct LwRegisters *r);


/* ---- toString / minor accessors ----------------------------------- */

/* Java: public String toString() { return name; } */
const char* lw_entity_to_string(const LwEntity *self);


#endif /* LW_ENTITY_H */
