/*
 * lw_weapon.h -- 1:1 port of weapons/Weapon.java
 *
 * Java: public class Weapon extends Item {
 *           private final List<EffectParameters> passiveEffects;
 *           Weapon(... ArrayNode effects, ... ArrayNode passiveEffects, ...) {
 *               super(id, cost, name, template, new Attack(..., Attack.TYPE_WEAPON, id, maxUses));
 *               this.attack.setItem(this);
 *               // populate passiveEffects[] from the JSON
 *           }
 *       }
 *
 * In C, "extends Item" is implemented by embedding LwItem as the first
 * field (so a `LwItem *` can target a LwWeapon).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/weapons/Weapon.java
 */
#ifndef LW_WEAPON_H
#define LW_WEAPON_H

#include "lw_item.h"
#include "lw_attack.h"
#include "lw_effect_params.h"

#include <stdint.h>


#ifndef LW_WEAPON_MAX_PASSIVES
#define LW_WEAPON_MAX_PASSIVES  16
#endif


typedef struct LwWeapon {
    LwItem               base;             /* extends Item */
    LwAttack             attack_storage;   /* The Java code does `new Attack(...)` and stores the pointer in
                                              base.attack -- here the storage lives inline so the registry can
                                              own the Weapon and base.attack points into &attack_storage. */
    LwEffectParameters   passive_effects[LW_WEAPON_MAX_PASSIVES];
    int                  n_passive_effects;
} LwWeapon;


/* Java:
 *   public Weapon(int id, int cost, int minRange, int maxRange,
 *                 ArrayNode effects, byte launchType, byte area,
 *                 boolean los, int template, String name,
 *                 ArrayNode passiveEffects, int maxUses)
 *
 * The C constructor takes flat LwEffectParameters arrays in place of
 * Jackson ArrayNodes (parsing happens in the loader layer).
 */
void lw_weapon_init(LwWeapon *self,
                    int id, int cost, int min_range, int max_range,
                    const LwEffectParameters *effects, int n_effects,
                    int8_t launch_type, int area, int los,
                    int template_, const char *name,
                    const LwEffectParameters *passive_effects, int n_passive_effects,
                    int max_uses);


/* Trivial getters -- inlined to mirror Java's getId / getTemplate /
 * getCost / getAttack / getName / getPassiveEffects(). */
static inline int           lw_weapon_get_id      (const LwWeapon *self) { return self->base.id; }
static inline int           lw_weapon_get_template(const LwWeapon *self) { return self->base.template_; }
static inline int           lw_weapon_get_cost    (const LwWeapon *self) { return self->base.cost; }
static inline LwAttack*     lw_weapon_get_attack  (LwWeapon *self)       { return self->base.attack; }
static inline const char*   lw_weapon_get_name    (const LwWeapon *self) { return self->base.name; }

static inline const LwEffectParameters* lw_weapon_get_passive_effects(const LwWeapon *self) {
    return self->passive_effects;
}
static inline int           lw_weapon_get_passive_effects_count(const LwWeapon *self) {
    return self->n_passive_effects;
}


/* public boolean isHandToHandWeapon() {
 *     return attack.getMinRange() == 1 && attack.getMaxRange() == 1;
 * }
 */
static inline int lw_weapon_is_hand_to_hand(const LwWeapon *self) {
    return self->base.attack
        && lw_attack_get_min_range(self->base.attack) == 1
        && lw_attack_get_max_range(self->base.attack) == 1;
}


/* public String toString() { return name; } */
static inline const char* lw_weapon_to_string(const LwWeapon *self) { return self->base.name; }


/* ---- Weapons static registry (weapons/Weapons.java) ---------------- *
 *
 * Java: private static Map<Integer, Weapon> weapons = new TreeMap<>();
 *       public static void addWeapon(Weapon weapon) { ... }
 *       public static Weapon getWeapon(int id) / getWeapon(String name)
 *       public static Map<Integer, Weapon> getTemplates()
 *
 * In C the registry lives in src_v2/lw_weapons.c. Capacity is sized so
 * that all known LeekWars weapons fit (~50 today).
 */

#ifndef LW_WEAPONS_MAX
#define LW_WEAPONS_MAX 256
#endif


/* public static void addWeapon(Weapon weapon)
 * Registers `weapon` in the static map and announces it to the Items
 * registry as TYPE_WEAPON. Caller retains ownership of the storage. */
void      lw_weapons_add_weapon(LwWeapon *weapon);

/* public static Weapon getWeapon(int id) -- NULL if unknown. */
LwWeapon *lw_weapons_get_weapon(int id);

/* public static Weapon getWeapon(String name) -- NULL if unknown.
 *
 * NOTE: Java's `.findFirst().get()` throws NoSuchElementException on
 * miss; the C variant returns NULL instead so callers can probe. */
LwWeapon *lw_weapons_get_weapon_by_name(const char *name);

/* public static Map<Integer, Weapon> getTemplates()
 *
 * Java returns the live map; in C we expose the underlying array +
 * count.  The pointers remain valid for the lifetime of the registry. */
LwWeapon **lw_weapons_get_templates(int *out_n);


#endif /* LW_WEAPON_H */
