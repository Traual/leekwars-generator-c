/*
 * lw_chip.h -- 1:1 port of chips/Chip.java + chips/ChipType.java
 *
 * Java: public class Chip extends Item {
 *           private final int cooldown;
 *           private final boolean teamCooldown;
 *           private final int initialCooldown;
 *           private final int level;
 *           private final ChipType chipType;
 *       }
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/chips/Chip.java
 *            java_reference/src/main/java/com/leekwars/generator/chips/ChipType.java
 */
#ifndef LW_CHIP_H
#define LW_CHIP_H

#include "lw_item.h"
#include "lw_attack.h"

#include <stdint.h>


/* ---- Java: enum ChipType ----------------------------------------- *
 *
 *   NONE, DAMAGE, HEAL, RETURN, PROTECTION, BOOST, POISON, SHACKLE,
 *   BULB, TACTIC
 *
 * Java enum ordinals (NONE=0, DAMAGE=1, ..., TACTIC=9). Preserved. */
typedef enum {
    LW_CHIP_TYPE_NONE       = 0,
    LW_CHIP_TYPE_DAMAGE     = 1,
    LW_CHIP_TYPE_HEAL       = 2,
    LW_CHIP_TYPE_RETURN     = 3,
    LW_CHIP_TYPE_PROTECTION = 4,
    LW_CHIP_TYPE_BOOST      = 5,
    LW_CHIP_TYPE_POISON     = 6,
    LW_CHIP_TYPE_SHACKLE    = 7,
    LW_CHIP_TYPE_BULB       = 8,
    LW_CHIP_TYPE_TACTIC     = 9
} LwChipType;


typedef struct LwChip {
    LwItem      base;             /* extends Item */
    LwAttack    attack_storage;   /* see lw_weapon.h: inline storage; base.attack points here */

    int         cooldown;         /* private final int cooldown */
    int         team_cooldown;    /* private final boolean teamCooldown */
    int         initial_cooldown; /* private final int initialCooldown */
    int         level;            /* private final int level */
    LwChipType  chip_type;        /* private final ChipType chipType */
} LwChip;


/* Java:
 *   public Chip(int id, int cost, int minRange, int maxRange,
 *               ArrayNode effects, byte launchType, byte area,
 *               boolean los, int cooldown, boolean teamCooldown,
 *               int initialCooldown, int level, int template,
 *               String name, ChipType chipType, int maxUses)
 */
void lw_chip_init(LwChip *self,
                  int id, int cost, int min_range, int max_range,
                  const LwEffectParameters *effects, int n_effects,
                  int8_t launch_type, int area, int los,
                  int cooldown, int team_cooldown, int initial_cooldown,
                  int level, int template_, const char *name,
                  LwChipType chip_type, int max_uses);


/* Trivial getters -- inlined to mirror Java's getCooldown / isTeamCooldown
 * / getInitialCooldown / getLevel / getChipType. */
static inline int        lw_chip_get_cooldown        (const LwChip *self) { return self->cooldown; }
static inline int        lw_chip_is_team_cooldown    (const LwChip *self) { return self->team_cooldown; }
static inline int        lw_chip_get_initial_cooldown(const LwChip *self) { return self->initial_cooldown; }
static inline int        lw_chip_get_level           (const LwChip *self) { return self->level; }
static inline LwChipType lw_chip_get_chip_type       (const LwChip *self) { return self->chip_type; }


/* Inherited getters (delegated through the embedded LwItem). */
static inline int          lw_chip_get_id      (const LwChip *self) { return self->base.id; }
static inline int          lw_chip_get_cost    (const LwChip *self) { return self->base.cost; }
static inline int          lw_chip_get_template(const LwChip *self) { return self->base.template_; }
static inline const char  *lw_chip_get_name    (const LwChip *self) { return self->base.name; }
static inline LwAttack    *lw_chip_get_attack  (LwChip *self)       { return self->base.attack; }


/* public String toString() { return name; } */
static inline const char* lw_chip_to_string(const LwChip *self) { return self->base.name; }


/* ---- Chips static registry (chips/Chips.java) ---------------------- *
 *
 * Java: private static Map<Integer, Chip> chips = new TreeMap<>();
 *       public static void addChip(Chip chip) { ... }
 *       public static Chip getChip(int id) / getChip(String name)
 *       public static Map<Integer, Chip> getTemplates()
 */

#ifndef LW_CHIPS_MAX
#define LW_CHIPS_MAX 512
#endif


/* public static void addChip(Chip chip)
 * Registers `chip` in the static map and announces it to the Items
 * registry as TYPE_CHIP. Caller retains ownership of the storage. */
void    lw_chips_add_chip(LwChip *chip);

/* public static Chip getChip(int id) -- NULL if unknown. */
LwChip *lw_chips_get_chip(int id);

/* public static Chip getChip(String name) -- NULL if unknown.
 *
 * Java's `.findFirst().get()` throws NoSuchElementException on miss;
 * the C variant returns NULL. */
LwChip *lw_chips_get_chip_by_name(const char *name);

/* public static Map<Integer, Chip> getTemplates() */
LwChip **lw_chips_get_templates(int *out_n);


#endif /* LW_CHIP_H */
