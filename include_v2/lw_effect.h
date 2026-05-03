/*
 * lw_effect.h -- 1:1 port of effect/Effect.java (the abstract base) and
 * dispatch hub for the 56 concrete EffectXxx subclasses.
 *
 * Java has 56 final classes extending the abstract Effect; the only
 * virtual methods are `apply(State)` and `applyStartTurn(State)`. We
 * collapse them into a single LwEffect struct + a switch-on-id dispatch
 * (lw_effect_apply / lw_effect_apply_start_turn) -- see PORTING_CONVENTIONS.md
 * "Polymorphism".
 *
 * The static factory `Effect.createEffect(...)` (Java line 188) lives here
 * as `lw_effect_create_effect` and is the only legitimate way to spawn an
 * effect; it owns the lifecycle (instantiate → setup fields → apply →
 * stack/replace → register on caster+target).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/effect/Effect.java
 */
#ifndef LW_EFFECT_H
#define LW_EFFECT_H

#include "lw_constants.h"
#include "lw_stats.h"
#include "lw_attack.h"     /* LwAttack + LwDamageType + lw_attack_get_* */
#include "lw_entity.h"     /* LwEntity + lw_entity_* getters/mutators */

/* Forward declarations -- the concrete struct definitions live elsewhere. */
struct LwState;
struct LwActions;
struct LwItem;


/*
 * Java: public abstract class Effect implements Cloneable
 *
 * Field order matches Java (see Effect.java around line 167-186) so that
 * a side-by-side diff stays trivial. `id` is private in Java but we keep
 * it as a plain field; getters live as inline below.
 *
 *   private int id;
 *   protected int turns = 0;
 *   protected double aoe = 1.0;
 *   protected double value1;
 *   protected double value2;
 *   protected boolean critical = false;
 *   protected double criticalPower = 1.0;
 *   protected Entity caster;
 *   protected Entity target;
 *   protected Attack attack;
 *   protected double jet;
 *   protected Stats stats = new Stats();
 *   protected int logID = 0;
 *   protected double erosionRate;
 *   public int value = 0;
 *   public int previousEffectTotalValue;
 *   public int targetCount;
 *   public int propagate = 0;
 *   public int modifiers = 0;
 *   protected EntityState state;
 */
typedef struct LwEffect {
    int                  id;                       /* LW_EFFECT_TYPE_* */
    int                  turns;
    double               aoe;
    double               value1;
    double               value2;
    int                  critical;                 /* boolean: 0/1 */
    double               critical_power;
    struct LwEntity     *caster;
    struct LwEntity     *target;
    LwAttack            *attack;
    double               jet;
    LwStats              stats;
    int                  log_id;
    double               erosion_rate;
    int                  value;
    int                  previous_effect_total_value;
    int                  target_count;
    int                  propagate;
    int                  modifiers;
    int                  state;                    /* EntityState ordinal, or LW_ENTITY_STATE_NONE */
} LwEffect;


/* ---- Constructors / lifecycle ---------------------------------------- */

/* Reset an LwEffect to "freshly default-constructed" state.  Mirrors the
 * implicit Java default-ctor + field initialisers (turns=0, aoe=1.0,
 * critical=false, criticalPower=1.0, stats=new Stats(), logID=0,
 * propagate=0, modifiers=0). */
void lw_effect_init(LwEffect *self);


/* Java:
 *   public static int createEffect(State state, int id, int turns, double aoe,
 *       double value1, double value2, boolean critical, Entity target,
 *       Entity caster, Attack attack, double jet, boolean stackable,
 *       int previousEffectTotalValue, int targetCount, int propagate,
 *       int modifiers)
 *
 * Returns the value applied by the effect (0 on failure, on stack,
 * or when the effect deals 0 damage). Mirrors Effect.createEffect
 * (Effect.java:188). The created effect (if any) is registered on the
 * target+caster; the caller does not need to free anything.
 *
 * NOTE: the Java code keeps a per-fight pool of Effect objects via
 * Entity.addEffect / Entity.addLaunchedEffect.  In C, those methods own
 * the LwEffect storage (typically a slot in `state->effects[]`).
 */
int lw_effect_create_effect(struct LwState *state, int id, int turns, double aoe,
                            double value1, double value2, int critical,
                            struct LwEntity *target, struct LwEntity *caster,
                            LwAttack *attack, double jet, int stackable,
                            int previous_effect_total_value, int target_count,
                            int propagate, int modifiers);


/* ---- Virtual dispatch ------------------------------------------------ */

/* Java: public void apply(State state) {}  -- subclasses override.
 *
 * Switch on self->id and call the matching lw_effect_<name>_apply.
 * For ids whose Java cell is `null` (33-36, 43, 50, 55-56, 58) and for
 * subclasses with empty bodies (EffectSummon, EffectTeleport, EffectPush,
 * EffectRepel, EffectResurrect, EffectAttract, EffectAllyKilledToAgility),
 * dispatch is a no-op -- matching Java exactly.
 */
void lw_effect_apply(LwEffect *self, struct LwState *state);

/* Java: public void applyStartTurn(State state) {}  -- subclasses override.
 *
 * Only EffectHeal, EffectPoison, and EffectAftereffect override this in
 * the Java reference; everything else is a no-op.
 */
void lw_effect_apply_start_turn(LwEffect *self, struct LwState *state);


/* ---- Inline accessors ------------------------------------------------ */

static inline int    lw_effect_get_id     (const LwEffect *self) { return self->id; }
static inline void   lw_effect_set_id     (LwEffect *self, int id) { self->id = id; }
static inline int    lw_effect_get_log_id (const LwEffect *self) { return self->log_id; }
static inline int    lw_effect_is_critical(const LwEffect *self) { return self->critical; }
static inline int    lw_effect_get_turns  (const LwEffect *self) { return self->turns; }
static inline void   lw_effect_set_turns  (LwEffect *self, int turns) { self->turns = turns; }
static inline double lw_effect_get_aoe    (const LwEffect *self) { return self->aoe; }
static inline int    lw_effect_get_value  (const LwEffect *self) { return self->value; }
static inline double lw_effect_get_value1 (const LwEffect *self) { return self->value1; }
static inline double lw_effect_get_value2 (const LwEffect *self) { return self->value2; }
static inline struct LwEntity* lw_effect_get_caster(const LwEffect *self) { return self->caster; }
static inline struct LwEntity* lw_effect_get_target(const LwEffect *self) { return self->target; }
static inline LwAttack*        lw_effect_get_attack(const LwEffect *self) { return self->attack; }
static inline int    lw_effect_get_modifiers(const LwEffect *self) { return self->modifiers; }
static inline int    lw_effect_get_state  (const LwEffect *self) { return self->state; }
static inline LwStats* lw_effect_get_stats(LwEffect *self) { return &self->stats; }
static inline void   lw_effect_set_target (LwEffect *self, struct LwEntity *e) { self->target = e; }
static inline void   lw_effect_set_caster (LwEffect *self, struct LwEntity *e) { self->caster = e; }


/* ---- Free methods (non-virtual) -------------------------------------- */

/* Java: public void addLog(State state)   (Effect.java:266) */
void lw_effect_add_log(LwEffect *self, struct LwState *state);

/* Java: public void reduce(double percent, Entity caster)   (Effect.java:329) */
void lw_effect_reduce(LwEffect *self, double percent, struct LwEntity *caster);

/* Java: public void mergeWith(Effect effect)   (Effect.java:341) */
void lw_effect_merge_with(LwEffect *self, const LwEffect *effect);

/* Java: public static int getEffectStat(int type)   (Effect.java:354)
 * Maps an effect type to the LW_STAT_* it draws from.  -1 if none. */
int lw_effect_get_effect_stat(int type);

/* Java: public Item getItem() -- returns this.attack != null ? attack.getItem() : null. */
struct LwItem* lw_effect_get_item(const LwEffect *self);


/* ---- Per-subclass apply prototypes ----------------------------------- *
 *
 * No separate header per subclass: each .c file in src_v2/lw_effect_xxx.c
 * implements one of these. They're declared here so lw_effect.c's switch
 * statement can call them.
 */

void lw_effect_damage_apply              (LwEffect *self, struct LwState *state);
void lw_effect_heal_apply                (LwEffect *self, struct LwState *state);
void lw_effect_heal_apply_start_turn     (LwEffect *self, struct LwState *state);
void lw_effect_buff_strength_apply       (LwEffect *self, struct LwState *state);
void lw_effect_buff_agility_apply        (LwEffect *self, struct LwState *state);
void lw_effect_relative_shield_apply     (LwEffect *self, struct LwState *state);
void lw_effect_absolute_shield_apply     (LwEffect *self, struct LwState *state);
void lw_effect_buff_mp_apply             (LwEffect *self, struct LwState *state);
void lw_effect_buff_tp_apply             (LwEffect *self, struct LwState *state);
void lw_effect_debuff_apply              (LwEffect *self, struct LwState *state);
void lw_effect_teleport_apply            (LwEffect *self, struct LwState *state); /* empty */
void lw_effect_permutation_apply         (LwEffect *self, struct LwState *state);
void lw_effect_vitality_apply            (LwEffect *self, struct LwState *state);
void lw_effect_poison_apply              (LwEffect *self, struct LwState *state);
void lw_effect_poison_apply_start_turn   (LwEffect *self, struct LwState *state);
void lw_effect_summon_apply              (LwEffect *self, struct LwState *state); /* empty */
void lw_effect_resurrect_apply           (LwEffect *self, struct LwState *state); /* empty */
void lw_effect_kill_apply                (LwEffect *self, struct LwState *state);
void lw_effect_shackle_mp_apply          (LwEffect *self, struct LwState *state);
void lw_effect_shackle_tp_apply          (LwEffect *self, struct LwState *state);
void lw_effect_shackle_strength_apply    (LwEffect *self, struct LwState *state);
void lw_effect_damage_return_apply       (LwEffect *self, struct LwState *state);
void lw_effect_buff_resistance_apply     (LwEffect *self, struct LwState *state);
void lw_effect_buff_wisdom_apply         (LwEffect *self, struct LwState *state);
void lw_effect_antidote_apply            (LwEffect *self, struct LwState *state);
void lw_effect_shackle_magic_apply       (LwEffect *self, struct LwState *state);
void lw_effect_aftereffect_apply         (LwEffect *self, struct LwState *state);
void lw_effect_aftereffect_apply_start_turn(LwEffect *self, struct LwState *state);
void lw_effect_vulnerability_apply       (LwEffect *self, struct LwState *state);
void lw_effect_absolute_vulnerability_apply(LwEffect *self, struct LwState *state);
void lw_effect_life_damage_apply         (LwEffect *self, struct LwState *state);
void lw_effect_steal_absolute_shield_apply(LwEffect *self, struct LwState *state);
void lw_effect_nova_damage_apply         (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_mp_apply         (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_tp_apply         (LwEffect *self, struct LwState *state);
void lw_effect_raw_absolute_shield_apply (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_strength_apply   (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_magic_apply      (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_science_apply    (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_agility_apply    (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_resistance_apply (LwEffect *self, struct LwState *state);
void lw_effect_raw_buff_wisdom_apply     (LwEffect *self, struct LwState *state);
void lw_effect_nova_vitality_apply       (LwEffect *self, struct LwState *state);
void lw_effect_attract_apply             (LwEffect *self, struct LwState *state); /* empty */
void lw_effect_shackle_agility_apply     (LwEffect *self, struct LwState *state);
void lw_effect_shackle_wisdom_apply      (LwEffect *self, struct LwState *state);
void lw_effect_remove_shackles_apply     (LwEffect *self, struct LwState *state);
void lw_effect_push_apply                (LwEffect *self, struct LwState *state); /* empty */
void lw_effect_raw_buff_power_apply      (LwEffect *self, struct LwState *state);
void lw_effect_repel_apply               (LwEffect *self, struct LwState *state); /* empty */
void lw_effect_raw_relative_shield_apply (LwEffect *self, struct LwState *state);
void lw_effect_ally_killed_to_agility_apply(LwEffect *self, struct LwState *state); /* empty */
void lw_effect_raw_heal_apply            (LwEffect *self, struct LwState *state);
void lw_effect_add_state_apply           (LwEffect *self, struct LwState *state);
void lw_effect_total_debuff_apply        (LwEffect *self, struct LwState *state);
void lw_effect_steal_life_apply          (LwEffect *self, struct LwState *state);
void lw_effect_multiply_stats_apply      (LwEffect *self, struct LwState *state);


/* All Entity getters/mutators called from the effect/ port live in
 * lw_entity.h (included above).  Attack helpers live in lw_attack.h.
 * State helpers live in lw_state.h, and action emit helpers in
 * lw_actions.h -- the per-subclass .c files include those directly. */


#endif /* LW_EFFECT_H */
