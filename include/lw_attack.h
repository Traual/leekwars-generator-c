/*
 * lw_attack.h -- 1:1 port of attack/Attack.java + attack/DamageType.java
 *
 * Java: public class Attack {
 *           private final int minRange, maxRange;
 *           private final boolean los;
 *           private final byte launchType;
 *           private final Area area;
 *           private int healAttack = 0;
 *           private int dammageAttack = 0;
 *           private final int attackType;
 *           private final int itemID;
 *           private Item item;
 *           private final int areaID;
 *           private final List<EffectParameters> effects;
 *           private final int maxUses;
 *       }
 *
 * applyOnCell() is THE central function for weapon/chip use; it draws
 * one RNG (`jet`) and then calls Effect.createEffect once per
 * (effect, target) pair. RNG draw count and effect iteration order
 * MUST match Java byte-for-byte.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/attack/Attack.java
 *            java_reference/src/main/java/com/leekwars/generator/attack/DamageType.java
 */
#ifndef LW_ATTACK_H
#define LW_ATTACK_H

#include "lw_constants.h"
#include "lw_area.h"
#include "lw_effect_params.h"

#include <stdint.h>


/* Forward declarations -- the actual struct definitions live in their
 * own headers (some still being ported in parallel). */
struct LwMap;
struct LwCell;
struct LwEntity;
struct LwState;
struct LwActions;
struct LwItem;


/* ---- Java: enum DamageType --------------------------------------- *
 *
 *   DIRECT(101), NOVA(107), RETURN(108), LIFE(109), POISON(110),
 *   AFTEREFFECT(110);
 *
 * NOTE: POISON and AFTEREFFECT share value 110 in Java -- both map
 * to ACTION_POISON_DAMAGE.  Preserved verbatim. */
typedef enum {
    LW_DAMAGE_TYPE_DIRECT      = 101,
    LW_DAMAGE_TYPE_NOVA        = 107,
    LW_DAMAGE_TYPE_RETURN      = 108,
    LW_DAMAGE_TYPE_LIFE        = 109,
    LW_DAMAGE_TYPE_POISON      = 110,
    LW_DAMAGE_TYPE_AFTEREFFECT = 110
} LwDamageType;


/* Capacity of the per-attack effect list. The largest LeekWars chip /
 * weapon has < 8 entries; 16 leaves room for future additions. */
#ifndef LW_ATTACK_MAX_EFFECTS
#define LW_ATTACK_MAX_EFFECTS  16
#endif


typedef struct LwAttack {
    int                  min_range;       /* private final int minRange */
    int                  max_range;       /* private final int maxRange */
    int                  los;              /* private final boolean los */
    int8_t               launch_type;     /* private final byte launchType */
    LwArea               area;             /* private final Area area */
    int                  heal_attack;     /* private int healAttack = 0 */
    int                  dammage_attack;  /* private int dammageAttack = 0 (Java sic) */
    int                  attack_type;     /* private final int attackType */
    int                  item_id;         /* private final int itemID */
    struct LwItem       *item;             /* private Item item */
    int                  area_id;         /* private final int areaID */

    /* private final List<EffectParameters> effects = new ArrayList<>(); */
    LwEffectParameters   effects[LW_ATTACK_MAX_EFFECTS];
    int                  n_effects;

    int                  max_uses;        /* private final int maxUses */
} LwAttack;


/* public Attack(int minRange, int maxRange, byte launchType, byte area,
 *               boolean los, ArrayNode effects, int attackType, int itemID,
 *               int maxUses)
 *
 * The Java constructor takes a Jackson ArrayNode of effect descriptors.
 * In C we accept a flat array of LwEffectParameters (the JSON parsing
 * happens in the loader layer, not here).  The healAttack /
 * dammageAttack bitfields are computed from the effect list, matching
 * Java's per-element bitwise-OR.
 */
void lw_attack_init(LwAttack *self,
                    int min_range, int max_range, int8_t launch_type,
                    int area, int los,
                    const LwEffectParameters *effects, int n_effects,
                    int attack_type, int item_id, int max_uses);


/* public int getArea() { return areaID; } */
int  lw_attack_get_area_id(const LwAttack *self);


/* public List<Cell> getTargetCells(Map map, Leek caster, Cell target)
 *
 * Out-buf variant: writes up to out_cap cells into out_buf and
 * returns the number written (mirrors the LwArea convention).
 */
int  lw_attack_get_target_cells_for_caster(const LwAttack *self,
                                           const struct LwMap *map,
                                           const struct LwEntity *caster,
                                           struct LwCell *target,
                                           struct LwCell *out_buf,
                                           int out_cap);


/* public List<Cell> getTargetCells(Map map, Cell cast_cell, Cell target)
 *
 * Caster-less variant (Java's second overload). */
int  lw_attack_get_target_cells_from_cell(const LwAttack *self,
                                          const struct LwMap *map,
                                          struct LwCell *cast_cell,
                                          struct LwCell *target,
                                          struct LwCell *out_buf,
                                          int out_cap);


/* public List<Entity> getWeaponTargets(State state, Entity caster, Cell target)
 *
 * Out-buf variant.  Returns the number of entities written into out_buf. */
int  lw_attack_get_weapon_targets(const LwAttack *self,
                                  struct LwState *state,
                                  struct LwEntity *caster,
                                  struct LwCell *target,
                                  struct LwEntity **out_buf,
                                  int out_cap);


/*
 * On suppose que l'autorisation de lancer le sort (minRange, maxRange,
 * launchType) a été vérifiée avant l'appel
 *
 * public List<Entity> applyOnCell(State state, Entity caster, Cell target,
 *                                  boolean critical)
 *
 * THE central function for weapon/chip use:
 *  - draws one RNG (jet) at the same code location as Java
 *  - iterates effects in order, calling Effect.createEffect for each
 *  - returns the deduplicated set of touched entities (out-buf).
 *
 * Returns the number of entities written into returnEntities. RNG draw
 * count and effect ordering are byte-for-byte identical to Java.
 */
int  lw_attack_apply_on_cell(LwAttack *self,
                             struct LwState *state,
                             struct LwEntity *caster,
                             struct LwCell *target,
                             int critical,
                             struct LwEntity **return_entities,
                             int return_cap);


/* private boolean filterTarget(int targets, Entity caster, Entity target)
 *
 * Exposed (non-static in C) so unit tests can hit it directly; in Java
 * it's private. The implementation matches Java line-for-line. */
int  lw_attack_filter_target(int targets,
                             struct LwEntity *caster,
                             struct LwEntity *target);


/* Compute the area effect attenuation : 100% at center, 50% on the border
 *
 * public double getPowerForCell(Cell target_cell, Cell current_cell)
 */
double lw_attack_get_power_for_cell(const LwAttack *self,
                                    const struct LwCell *target_cell,
                                    const struct LwCell *current_cell);


/* Trivial getters -- inlined to mirror Java's getMinRange / getMaxRange /
 * needLos / getLaunchType / etc. */
static inline int    lw_attack_get_min_range  (const LwAttack *self) { return self->min_range; }
static inline int    lw_attack_get_max_range  (const LwAttack *self) { return self->max_range; }
static inline int8_t lw_attack_get_launch_type(const LwAttack *self) { return self->launch_type; }
static inline int    lw_attack_need_los       (const LwAttack *self) { return self->los; }


/* public List<EffectParameters> getEffects() -- exposed via direct
 * pointer + count (caller treats as read-only array). */
static inline const LwEffectParameters* lw_attack_get_effects(const LwAttack *self) { return self->effects; }
static inline int                       lw_attack_get_effects_count(const LwAttack *self) { return self->n_effects; }


/* public EffectParameters getEffectParametersByType(int type)
 * Returns NULL if no effect with that id. */
const LwEffectParameters* lw_attack_get_effect_parameters_by_type(const LwAttack *self, int type);


/* public boolean isHealAttack(int target)   -> (healAttack & target) != 0 */
static inline int lw_attack_is_heal_attack(const LwAttack *self, int target) {
    return (self->heal_attack & target) != 0;
}

/* public boolean isDamageAttack(int target) -> (dammageAttack & target) != 0 */
static inline int lw_attack_is_damage_attack(const LwAttack *self, int target) {
    return (self->dammage_attack & target) != 0;
}

/* public int getItemId() { return itemID; } */
static inline int lw_attack_get_item_id(const LwAttack *self) { return self->item_id; }

/* public int getType() { return attackType; } */
static inline int lw_attack_get_type(const LwAttack *self) { return self->attack_type; }


/* public boolean needsEmptyCell()
 * True iff the attack contains a TELEPORT/SUMMON/RESURRECT effect. */
int lw_attack_needs_empty_cell(const LwAttack *self);


/* public void setItem(Item item) / public Item getItem() */
static inline void           lw_attack_set_item(LwAttack *self, struct LwItem *item) { self->item = item; }
static inline struct LwItem* lw_attack_get_item(const LwAttack *self) { return self->item; }


/* public long getMaxUses() { return maxUses; } */
static inline int lw_attack_get_max_uses(const LwAttack *self) { return self->max_uses; }


#endif /* LW_ATTACK_H */
