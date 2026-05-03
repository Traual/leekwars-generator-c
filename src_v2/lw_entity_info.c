/*
 * lw_entity_info.c -- 1:1 port of scenario/EntityInfo.java
 *
 * The Java class is a JSON-fed POD with one createEntity() method that
 * instantiates a Leek/Bulb/Turret via reflection and applies all the
 * stored characteristics.
 *
 * The reflection bit collapses to a tag-based dispatch in C (the entity
 * subclass is just an int field on LwEntity in our port). The setters
 * (entity.setLife / setStrength / ...) are forward-declared here and
 * supplied by lw_entity.c (ported in parallel).
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/scenario/EntityInfo.java
 */
#include "lw_entity_info.h"

#include <stddef.h>
#include <string.h>

#include "lw_constants.h"


/* ---- forward declarations supplied by entity / weapons / chips / scenario.
 * These mirror the Java setters one-for-one. The bodies live in their own
 * .c files (lw_entity.c, lw_weapons.c, lw_chips.c, lw_scenario.c). */
struct LwEntity* lw_entity_new(int type);          /* allocates a fresh entity */
void lw_entity_set_id(struct LwEntity *self, int id);
void lw_entity_set_name(struct LwEntity *self, const char *name);
void lw_entity_set_level(struct LwEntity *self, int level);
void lw_entity_set_total_life(struct LwEntity *self, int life);
void lw_entity_set_life(struct LwEntity *self, int life);
void lw_entity_set_strength(struct LwEntity *self, int strength);
void lw_entity_set_agility(struct LwEntity *self, int agility);
void lw_entity_set_wisdom(struct LwEntity *self, int wisdom);
void lw_entity_set_resistance(struct LwEntity *self, int resistance);
void lw_entity_set_science(struct LwEntity *self, int science);
void lw_entity_set_magic(struct LwEntity *self, int magic);
void lw_entity_set_frequency(struct LwEntity *self, int frequency);
void lw_entity_set_cores(struct LwEntity *self, int cores);
void lw_entity_set_ram(struct LwEntity *self, int ram);
void lw_entity_set_tp(struct LwEntity *self, int tp);
void lw_entity_set_mp(struct LwEntity *self, int mp);
void lw_entity_set_farmer(struct LwEntity *self, int farmer);
void lw_entity_set_dead(struct LwEntity *self, int dead);
void lw_entity_set_orientation(struct LwEntity *self, int orientation);
void lw_entity_set_farmer_name(struct LwEntity *self, const char *name);
void lw_entity_set_farmer_country(struct LwEntity *self, const char *country);
void lw_entity_set_ai_name(struct LwEntity *self, const char *ai);
void lw_entity_set_team_id(struct LwEntity *self, int team);
void lw_entity_set_team_name(struct LwEntity *self, const char *name);
void lw_entity_set_composition_name(struct LwEntity *self, const char *name);
void lw_entity_set_skin(struct LwEntity *self, int skin);
void lw_entity_set_hat(struct LwEntity *self, int hat);
void lw_entity_set_metal(struct LwEntity *self, int metal);
void lw_entity_set_face(struct LwEntity *self, int face);
void lw_entity_set_initial_cell(struct LwEntity *self, int cell);

struct LwWeapon;
struct LwChip;
struct LwWeapon* lw_weapons_get_weapon(int id);
struct LwChip*   lw_chips_get_chip   (int id);
void lw_entity_add_weapon(struct LwEntity *self, struct LwWeapon *weapon);
void lw_entity_add_chip  (struct LwEntity *self, struct LwChip *chip);

/* Forward decl into lw_scenario.c -- mirrors scenario.getFarmer / scenario.teams */
struct LwScenario;
const LwFarmerInfo* lw_scenario_get_farmer(const struct LwScenario *scenario, int farmer);
const LwTeamInfo*   lw_scenario_get_team  (const struct LwScenario *scenario, int team);


/* Java: public EntityInfo() {} -- struct field defaults. */
void lw_entity_info_init(LwEntityInfo *self) {
    if (self == NULL) return;
    memset(self, 0, sizeof(*self));
    self->ai_file = NULL;
}


/* Java:
 *   public Entity createEntity(Generator generator, Scenario scenario, Fight fight) {
 *       Entity entity;
 *       try {
 *           var clazz = this.customClass != null ? this.customClass : classes[type];
 *           entity = (Entity) clazz.getDeclaredConstructor().newInstance();
 *       } catch (...) {
 *           generator.exception(e, fight);
 *           return null;
 *       }
 *       entity.setId(id);
 *       ...
 *   }
 *
 * NOTE: the reflection lookup collapses to the customClass override
 * (tested only) or the type tag straight into LwEntity's allocator.
 */
struct LwEntity* lw_entity_info_create_entity(const LwEntityInfo *self,
                                              struct LwGenerator *generator,
                                              struct LwScenario *scenario,
                                              struct LwFight *fight) {
    (void)generator;  /* errors are reported via the binding, not here */
    (void)fight;

    int actual_type = (self->custom_class != 0) ? self->custom_class : self->type;
    struct LwEntity *entity = lw_entity_new(actual_type);
    if (entity == NULL) {
        /* Java: generator.exception(e, fight); return null; */
        return NULL;
    }

    lw_entity_set_id(entity, self->id);
    lw_entity_set_name(entity, self->name);
    lw_entity_set_level(entity, self->level);
    lw_entity_set_total_life(entity, self->life);
    lw_entity_set_life(entity, self->life);
    lw_entity_set_strength(entity, self->strength);
    lw_entity_set_agility(entity, self->agility);
    lw_entity_set_wisdom(entity, self->wisdom);
    lw_entity_set_resistance(entity, self->resistance);
    lw_entity_set_science(entity, self->science);
    lw_entity_set_magic(entity, self->magic);
    lw_entity_set_frequency(entity, self->frequency);
    lw_entity_set_cores(entity, self->cores);
    lw_entity_set_ram(entity, self->ram);
    lw_entity_set_tp(entity, self->tp);
    lw_entity_set_mp(entity, self->mp);
    lw_entity_set_farmer(entity, self->farmer);
    lw_entity_set_dead(entity, self->dead);
    lw_entity_set_orientation(entity, self->orientation);

    if (self->farmer >= 0) {
        const LwFarmerInfo *farmer = lw_scenario_get_farmer(scenario, self->farmer);
        if (farmer != NULL) {
            lw_entity_set_farmer_name(entity, farmer->name);
            lw_entity_set_farmer_country(entity, farmer->country);
        }
    }

    lw_entity_set_ai_name(entity, self->ai);
    lw_entity_set_team_id(entity, self->team);

    if (self->team > 0) {
        const LwTeamInfo *team = lw_scenario_get_team(scenario, self->team);
        if (team != NULL) {
            lw_entity_set_team_name(entity, team->name);
            if (team->has_composition_name) {
                lw_entity_set_composition_name(entity, team->composition_name);
            }
        }
    }

    lw_entity_set_skin(entity, self->skin);
    lw_entity_set_hat(entity, self->hat);
    lw_entity_set_metal(entity, self->metal);
    lw_entity_set_face(entity, self->face);
    /* entity.setInitialCell(cell);  -- Java passes the Integer (may be
     * null). In C: only set when has_cell, leaving the entity with a
     * zero-initialised "absent" initial cell otherwise. */
    if (self->has_cell) {
        lw_entity_set_initial_cell(entity, self->cell);
    }

    /* for (Object w : weapons) {
     *     var weapon = Weapons.getWeapon((Integer) w);
     *     if (weapon == null) Log.e(...); else entity.addWeapon(weapon);
     * } */
    for (int i = 0; i < self->n_weapons; i++) {
        struct LwWeapon *weapon = lw_weapons_get_weapon(self->weapons[i]);
        if (weapon == NULL) {
            /* Java: Log.e(TAG, "No such weapon: " + w); */
        } else {
            lw_entity_add_weapon(entity, weapon);
        }
    }

    /* for (Object c : chips) {
     *     Integer chip = (Integer) c;
     *     entity.addChip(Chips.getChip(chip));
     * } */
    for (int i = 0; i < self->n_chips; i++) {
        struct LwChip *chip = lw_chips_get_chip(self->chips[i]);
        lw_entity_add_chip(entity, chip);
    }

    return entity;
}
