/*
 * lw_chips.c -- 1:1 port of chips/Chips.java + the Chip constructor
 * (chips/Chip.java).
 *
 * Java: public class Chips {
 *           private static Map<Integer, Chip> chips = new TreeMap<>();
 *           public static void addChip(Chip chip) {
 *               chips.put(chip.getId(), chip);
 *               Items.addChip(chip.getId());
 *           }
 *           public static Chip getChip(int id) { return chips.get(id); }
 *           public static Chip getChip(String name) { ... }
 *           public static Map<Integer, Chip> getTemplates() { return chips; }
 *       }
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/chips/Chips.java
 *            java_reference/src/main/java/com/leekwars/generator/chips/Chip.java
 */
#include "lw_chip.h"

#include "lw_attack.h"
#include "lw_item.h"
#include "lw_constants.h"

#include <stddef.h>
#include <string.h>


/* Items.addChip(id) -- the items registry is owned by the Items
 * loader (not yet ported). Forward-declare so the link resolves once
 * lw_items.c lands. */
void lw_items_add_chip(int id);


/* ---- Chip constructor (Chip.java:15-24) -------------------------- */

/* public Chip(int id, int cost, int minRange, int maxRange,
 *             ArrayNode effects, byte launchType, byte area,
 *             boolean los, int cooldown, boolean teamCooldown,
 *             int initialCooldown, int level, int template,
 *             String name, ChipType chipType, int maxUses) */
void lw_chip_init(LwChip *self,
                  int id, int cost, int min_range, int max_range,
                  const LwEffectParameters *effects, int n_effects,
                  int8_t launch_type, int area, int los,
                  int cooldown, int team_cooldown, int initial_cooldown,
                  int level, int template_, const char *name,
                  LwChipType chip_type, int max_uses) {

    /* super(id, cost, name, template, new Attack(minRange, maxRange,
     *      launchType, area, los, effects, Attack.TYPE_CHIP, id, maxUses)); */
    lw_attack_init(&self->attack_storage,
                   min_range, max_range, launch_type, area, los,
                   effects, n_effects,
                   LW_ATTACK_TYPE_CHIP, id, max_uses);
    lw_item_init(&self->base, id, cost, name, template_, &self->attack_storage);

    /* this.attack.setItem(this); */
    lw_attack_set_item(&self->attack_storage, &self->base);

    self->cooldown = cooldown;
    self->team_cooldown = team_cooldown;
    self->initial_cooldown = initial_cooldown;
    self->level = level;
    self->chip_type = chip_type;
}


/* ---- Chips static registry (Chips.java) -------------------------- *
 *
 * Same layout as the weapons registry (sorted-by-id array + linear /
 * binary lookup).  See lw_weapons.c for the rationale.
 */
static LwChip *s_chips[LW_CHIPS_MAX];
static int     s_n_chips = 0;


/* public static void addChip(Chip chip) {
 *     chips.put(chip.getId(), chip);
 *     Items.addChip(chip.getId());
 * }
 */
void lw_chips_add_chip(LwChip *chip) {
    if (chip == NULL || s_n_chips >= LW_CHIPS_MAX) return;
    int id = lw_chip_get_id(chip);

    /* TreeMap.put: replace if key exists. */
    for (int i = 0; i < s_n_chips; i++) {
        if (lw_chip_get_id(s_chips[i]) == id) {
            s_chips[i] = chip;
            lw_items_add_chip(id);
            return;
        }
    }

    /* Insertion sort by id ascending (TreeMap iteration order). */
    int pos = s_n_chips;
    while (pos > 0 && lw_chip_get_id(s_chips[pos - 1]) > id) {
        s_chips[pos] = s_chips[pos - 1];
        pos--;
    }
    s_chips[pos] = chip;
    s_n_chips++;

    lw_items_add_chip(id);
}


/* public static Chip getChip(int id) { return chips.get(id); } */
LwChip *lw_chips_get_chip(int id) {
    for (int i = 0; i < s_n_chips; i++) {
        if (lw_chip_get_id(s_chips[i]) == id) return s_chips[i];
    }
    return NULL;
}


/* public static Chip getChip(String name) {
 *     return chips.values().stream().filter(c -> c.getName().equals(name))
 *                                   .findFirst().get();
 * }
 *
 * Java throws NoSuchElementException on miss; we return NULL. */
LwChip *lw_chips_get_chip_by_name(const char *name) {
    if (name == NULL) return NULL;
    for (int i = 0; i < s_n_chips; i++) {
        const char *n = lw_chip_get_name(s_chips[i]);
        if (n != NULL && strcmp(n, name) == 0) return s_chips[i];
    }
    return NULL;
}


/* public static Map<Integer, Chip> getTemplates() { return chips; } */
LwChip **lw_chips_get_templates(int *out_n) {
    if (out_n) *out_n = s_n_chips;
    return s_chips;
}
