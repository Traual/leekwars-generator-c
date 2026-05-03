/*
 * lw_attack.c -- 1:1 port of attack/Attack.java
 *
 * applyOnCell() (Attack.java:155-257) is THE central function for
 * weapon/chip use; it draws one RNG ("jet") and then walks the
 * effects list calling Effect.createEffect once per effect/target
 * pair.  The order of operations -- the jet draw, the propagate /
 * teleport / push / attract dispatch, and the for-each-effect loop --
 * MUST match Java byte-for-byte (byte-for-byte parity test compares
 * action streams).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/attack/Attack.java
 */
#include "lw_attack.h"

#include "lw_constants.h"
#include "lw_effect.h"
#include "lw_pathfinding.h"
#include "lw_rng.h"
#include "lw_cell.h"

#include <stddef.h>


/* ---- Forward decls for the engine APIs we call --------------------
 *
 * Map / State / Entity live in their own (mostly not-yet-ported)
 * headers. We only need the function signatures here; the linker
 * resolves them once those modules exist. */

/* maps/Map.java */
LwCell *lw_map_get_attract_last_available_cell(struct LwMap *map,
                                               LwCell *from, LwCell *target,
                                               LwCell *caster_cell);
LwCell *lw_map_get_push_last_available_cell   (struct LwMap *map,
                                               LwCell *from, LwCell *target,
                                               LwCell *caster_cell);

/* maps/Cell.java -- cell.getPlayer(map) is declared in lw_cell.h. */

/* state/State.java
 *
 * NOTE: the LwState struct definition is being ported in parallel; we
 * declare the methods we need here.  RNG access goes through
 * lw_state_get_rng(state) -> &state->rng_n so the convention from
 * PORTING_CONVENTIONS.md ("equivalent lw_rng_double(&state->rng_n)
 * call at the same code location in C") is preserved at the call
 * site by callers that have direct field access; the accessor wrapper
 * is provided so this file doesn't need the full LwState definition.
 */
struct LwMap *lw_state_get_map(const struct LwState *state);
uint64_t     *lw_state_get_rng (struct LwState *state);
void          lw_state_slide_entity   (struct LwState *state, struct LwEntity *e,
                                       LwCell *destination, struct LwEntity *caster);
void          lw_state_teleport_entity(struct LwState *state, struct LwEntity *e,
                                       LwCell *destination, struct LwEntity *caster,
                                       int item_id);

/* state/Entity.java */
LwCell *lw_entity_get_cell  (const struct LwEntity *e);
int     lw_entity_get_team  (const struct LwEntity *e);
int     lw_entity_is_alive  (const struct LwEntity *e);
int     lw_entity_is_summon (const struct LwEntity *e);
int     lw_entity_has_effect(const struct LwEntity *e, int item_id);

/* Effect.createEffect comes from lw_effect.h via lw_effect_create_effect. */


/* ---- Internal helpers --------------------------------------------- */

/* TreeMap<Integer, Double> areaFactors -- keys are entity FIDs.  We
 * use a flat (fid,value) array because the maximum size is bounded by
 * the area cell count (<= ~26).  Walk linearly for lookup -- iteration
 * order matches Java's TreeMap (key ascending) only for the put loop
 * order, but lookups are by key so order doesn't matter for the get. */
typedef struct {
    int    fid;
    double value;
} LwFidDouble;


static void lw_fid_double_put(LwFidDouble *map, int *n_map, int fid, double value) {
    for (int i = 0; i < *n_map; i++) {
        if (map[i].fid == fid) {
            map[i].value = value;
            return;
        }
    }
    map[*n_map].fid = fid;
    map[*n_map].value = value;
    (*n_map)++;
}


static double lw_fid_double_get(const LwFidDouble *map, int n_map, int fid) {
    for (int i = 0; i < n_map; i++) {
        if (map[i].fid == fid) return map[i].value;
    }
    return 0.0;
}


static int lw_entity_list_contains(struct LwEntity * const *list, int n,
                                   const struct LwEntity *target) {
    for (int i = 0; i < n; i++) {
        if (list[i] == target) return 1;
    }
    return 0;
}


/* ---- Constructor (Attack.java:66-96) ------------------------------ */

/* public Attack(int minRange, int maxRange, byte launchType, byte area,
 *               boolean los, ArrayNode effects, int attackType, int itemID,
 *               int maxUses) */
void lw_attack_init(LwAttack *self,
                    int min_range, int max_range, int8_t launch_type,
                    int area, int los,
                    const LwEffectParameters *effects, int n_effects,
                    int attack_type, int item_id, int max_uses) {
    self->min_range = min_range;
    self->max_range = max_range;
    self->launch_type = launch_type;
    self->los = los;
    self->attack_type = attack_type;
    self->item_id = item_id;
    self->max_uses = max_uses;
    self->item = NULL;
    self->heal_attack = 0;
    self->dammage_attack = 0;

    self->area_id = area;
    /* this.area = Area.getArea(this, area); */
    lw_area_get_area_for_type(&self->area, self, area);

    self->n_effects = 0;

    /* On charge ensuite la liste des effets */
    for (int i = 0; i < n_effects; i++) {
        const LwEffectParameters *e = &effects[i];
        int    type      = e->id;
        double value1    = e->value1;
        double value2    = e->value2;
        int    turns     = e->turns;
        int    targets   = e->targets;
        int    modifiers = e->modifiers;
        if (type == LW_EFFECT_TYPE_HEAL) {
            self->heal_attack |= targets;
        }
        if (type == LW_EFFECT_TYPE_DAMAGE || type == LW_EFFECT_TYPE_POISON) {
            self->dammage_attack |= targets;
        }
        if (self->n_effects < LW_ATTACK_MAX_EFFECTS) {
            lw_effect_parameters_init(&self->effects[self->n_effects],
                                      type, value1, value2, turns, targets, modifiers);
            self->n_effects++;
        }
    }
}


/* public int getArea() { return areaID; } */
int lw_attack_get_area_id(const LwAttack *self) {
    return self->area_id;
}


/* public List<Cell> getTargetCells(Map map, Leek caster, Cell target)
 *
 * On récupère les cases cibles
 *   return area.getArea(map, caster.getCell(), target, caster);
 */
int lw_attack_get_target_cells_for_caster(const LwAttack *self,
                                          const struct LwMap *map,
                                          const struct LwEntity *caster,
                                          struct LwCell *target,
                                          struct LwCell *out_buf,
                                          int out_cap) {
    /* NOTE: the Java signature uses Leek but the implementation only
     * needs caster.getCell() and a non-null caster pointer. We pass the
     * Entity straight through; lw_area_get_area takes a struct LwLeek*
     * which the area code casts back to whatever it needs. */
    LwCell *cast_cell = lw_entity_get_cell(caster);
    return lw_area_get_area(&self->area, map, cast_cell, target,
                            (struct LwLeek *)caster, out_buf, out_cap);
}


/* public List<Cell> getTargetCells(Map map, Cell cast_cell, Cell target)
 *
 * On récupère les cases cibles
 *   return area.getArea(map, cast_cell, target, null);
 */
int lw_attack_get_target_cells_from_cell(const LwAttack *self,
                                         const struct LwMap *map,
                                         struct LwCell *cast_cell,
                                         struct LwCell *target,
                                         struct LwCell *out_buf,
                                         int out_cap) {
    return lw_area_get_area(&self->area, map, cast_cell, target,
                            NULL, out_buf, out_cap);
}


/* public List<Entity> getWeaponTargets(State state, Entity caster, Cell target)
 *
 * Returns the deduplicated set of entities that an effect-list pass
 * over (area cells × effects) would touch. Used by the AI to predict
 * targets before commit.
 */
int lw_attack_get_weapon_targets(const LwAttack *self,
                                 struct LwState *state,
                                 struct LwEntity *caster,
                                 struct LwCell *target,
                                 struct LwEntity **out_buf,
                                 int out_cap) {

    int n_return = 0;

    /* On suppose que l'autorisation de lancer le sort (minScope, maxScope,
     * launchType) a été vérifiée avant l'appel */

    /* On récupère les cases cibles
     *
     * NOTE: lw_area_get_area writes LwCell *values* into out_buf (see
     * src_v2/lw_area.c).  Cell.getPlayer(map) keys by cell.id, so the
     * value-copy is fine for the lookup. */
    LwCell target_cells[64];
    int n_target_cells = lw_area_get_area(&self->area, lw_state_get_map(state),
                                          lw_entity_get_cell(caster), target,
                                          (struct LwLeek *)caster,
                                          target_cells, 64);

    /* On trouve les poireaux sur ces cellules */
    struct LwEntity *target_entities[64];
    int n_target_entities = 0;
    for (int i = 0; i < n_target_cells; i++) {
        LwCell *cell = &target_cells[i];
        struct LwEntity *p = lw_cell_get_player(cell, lw_state_get_map(state));
        if (p != NULL) {
            target_entities[n_target_entities++] = p;
        }
    }

    /* Puis on applique les effets */
    for (int e = 0; e < self->n_effects; e++) {
        const LwEffectParameters *parameters = &self->effects[e];
        for (int t = 0; t < n_target_entities; t++) {
            struct LwEntity *target_leek = target_entities[t];
            if (lw_entity_is_dead((struct LwEntity *)target_leek)) {
                continue;
            }
            if (!lw_attack_filter_target(parameters->targets, caster, target_leek)) {
                continue;
            }
            if (!lw_entity_list_contains(out_buf, n_return, target_leek)) {
                if (n_return < out_cap) {
                    out_buf[n_return++] = target_leek;
                }
            }
        }
        /* Always caster? */
        if ((parameters->modifiers & LW_MODIFIER_ON_CASTER) != 0
            && !lw_entity_list_contains(out_buf, n_return, caster)) {
            if (n_return < out_cap) {
                out_buf[n_return++] = caster;
            }
        }
    }
    return n_return;
}


/*
 * On suppose que l'autorisation de lancer le sort (minRange, maxRange, launchType) a été vérifiée avant l'appel
 *
 * public List<Entity> applyOnCell(State state, Entity caster, Cell target,
 *                                  boolean critical)
 */
int lw_attack_apply_on_cell(LwAttack *self,
                            struct LwState *state,
                            struct LwEntity *caster,
                            struct LwCell *target,
                            int critical,
                            struct LwEntity **return_entities,
                            int return_cap) {

    int n_return = 0;

    /* On récupère les cases cibles
     *
     * NOTE: lw_area_get_area writes LwCell values; we iterate by
     * &target_cells[i] below (Cell.getPlayer keys by id, so value-copy
     * is safe for the lookup). */
    LwCell target_cells[64];
    int n_target_cells = lw_area_get_area(&self->area, lw_state_get_map(state),
                                          lw_entity_get_cell(caster), target,
                                          (struct LwLeek *)caster,
                                          target_cells, 64);

    /* On trouve les poireaux sur ces cellules */
    struct LwEntity *target_entities[64];
    int n_target_entities = 0;

    /* Facteurs de zones pour chaque entité */
    LwFidDouble area_factors[64];
    int n_area_factors = 0;

    for (int i = 0; i < n_target_cells; i++) {
        LwCell *cell = &target_cells[i];
        struct LwEntity *p = lw_cell_get_player(cell, lw_state_get_map(state));
        if (p != NULL && lw_entity_is_alive(p)) {
            target_entities[n_target_entities++] = p;
            lw_fid_double_put(area_factors, &n_area_factors,
                              lw_entity_get_fid(p),
                              lw_attack_get_power_for_cell(self, target, cell));
        }
    }

    /* On défini le jet */
    double jet = lw_rng_get_double(lw_state_get_rng(state));

    /* Apply effects */
    int previous_effect_total_value = 0;
    int propagate = 0;

    for (int ei = 0; ei < self->n_effects; ei++) {
        const LwEffectParameters *parameters = &self->effects[ei];

        if (lw_entity_is_dead(caster)) continue;

        if (parameters->id == LW_EFFECT_TYPE_ATTRACT) {
            for (int t = 0; t < n_target_entities; t++) {
                struct LwEntity *entity = target_entities[t];
                /* Attract directly to target cell */
                LwCell *destination = lw_map_get_attract_last_available_cell(
                    lw_state_get_map(state),
                    lw_entity_get_cell(entity), target,
                    lw_entity_get_cell(caster));
                lw_state_slide_entity(state, entity, destination, caster);
            }
        } else if (parameters->id == LW_EFFECT_TYPE_PUSH) {
            for (int t = 0; t < n_target_entities; t++) {
                struct LwEntity *entity = target_entities[t];
                /* Find last available position to push */
                LwCell *destination = lw_map_get_push_last_available_cell(
                    lw_state_get_map(state),
                    lw_entity_get_cell(entity), target,
                    lw_entity_get_cell(caster));
                lw_state_slide_entity(state, entity, destination, caster);
            }
        }

        if (parameters->id == LW_EFFECT_TYPE_TELEPORT) {

            lw_state_teleport_entity(state, caster, target, caster, self->item_id);
            /* Java: returnEntities.add(caster);  (unconditional add) */
            if (n_return < return_cap) {
                return_entities[n_return++] = caster;
            }

        } else if (parameters->id == LW_EFFECT_TYPE_PROPAGATION) {

            propagate = (int)parameters->value1;

        } else {

            int modifiers = parameters->modifiers;
            int on_caster = (modifiers & LW_MODIFIER_ON_CASTER) != 0;
            int stackable = (modifiers & LW_MODIFIER_STACKABLE) != 0;
            int effect_total_value = 0;
            int multiplied_by_target_count = (modifiers & LW_MODIFIER_MULTIPLIED_BY_TARGETS) != 0;
            int not_replaceable = (modifiers & LW_MODIFIER_NOT_REPLACEABLE) != 0;

            struct LwEntity *effect_target_entities[64];
            int n_effect_targets = 0;

            for (int t = 0; t < n_target_entities; t++) {
                struct LwEntity *target_entity = target_entities[t];
                if (lw_entity_is_dead(target_entity)) continue;
                if (!lw_attack_filter_target(parameters->targets, caster, target_entity)) {
                    continue;
                }
                if (on_caster && target_entity == caster) {
                    continue;
                }
                /* L'effet est déjà sur la cible et pas remplaçable */
                if (not_replaceable && lw_entity_has_effect(target_entity, self->item_id)) {
                    continue;
                }
                if (!lw_entity_list_contains(return_entities, n_return, target_entity)) {
                    if (n_return < return_cap) {
                        return_entities[n_return++] = target_entity;
                    }
                }
                effect_target_entities[n_effect_targets++] = target_entity;
            }
            int target_count = multiplied_by_target_count ? n_effect_targets : 1;

            if (!on_caster) { /* If the effect is on caster, we only count the targets, not apply the effect */
                for (int t = 0; t < n_effect_targets; t++) {
                    struct LwEntity *target_entity = effect_target_entities[t];

                    double aoe = lw_fid_double_get(area_factors, n_area_factors,
                                                   lw_entity_get_fid(target_entity));

                    effect_total_value += lw_effect_create_effect(
                        state, parameters->id, parameters->turns, aoe,
                        parameters->value1, parameters->value2, critical,
                        target_entity, caster, self, jet, stackable,
                        previous_effect_total_value, target_count, propagate, modifiers);
                }
            }

            /* Always caster */
            if (on_caster) {
                if (n_return < return_cap) {
                    return_entities[n_return++] = caster;
                }
                lw_effect_create_effect(state, parameters->id, parameters->turns, 1,
                                        parameters->value1, parameters->value2, critical,
                                        caster, caster, self, jet, stackable,
                                        previous_effect_total_value, target_count, propagate, modifiers);
            }

            previous_effect_total_value = effect_total_value;
        }
    }
    return n_return;
}


/* private boolean filterTarget(int targets, Entity caster, Entity target) */
int lw_attack_filter_target(int targets,
                            struct LwEntity *caster,
                            struct LwEntity *target) {

    /* Enemies */
    if ((targets & LW_TARGET_ENEMIES) == 0
        && lw_entity_get_team(caster) != lw_entity_get_team(target)) {
        return 0;
    }

    /* Allies */
    if ((targets & LW_TARGET_ALLIES) == 0
        && lw_entity_get_team(caster) == lw_entity_get_team(target)) {
        return 0;
    }

    /* Caster */
    if ((targets & LW_TARGET_CASTER) == 0 && caster == target) {
        return 0;
    }

    /* Non-Summons */
    if ((targets & LW_TARGET_NON_SUMMONS) == 0 && !lw_entity_is_summon(target)) {
        return 0;
    }

    /* Summons */
    if ((targets & LW_TARGET_SUMMONS) == 0 && lw_entity_is_summon(target)) {
        return 0;
    }

    return 1;
}


/* Compute the area effect attenuation : 100% at center, 50% on the border
 *
 * public double getPowerForCell(Cell target_cell, Cell current_cell)
 */
double lw_attack_get_power_for_cell(const LwAttack *self,
                                    const struct LwCell *target_cell,
                                    const struct LwCell *current_cell) {

    /* if (area instanceof AreaLaserLine || area instanceof AreaFirstInLine
     *     || area instanceof AreaAllies || area instanceof AreaEnemies) */
    if (self->area.type == LW_AREA_TAG_LASER_LINE
        || self->area.type == LW_AREA_TAG_FIRST_IN_LINE
        || self->area.type == LW_AREA_TAG_ALLIES
        || self->area.type == LW_AREA_TAG_ENEMIES) {
        return 1.0;
    }

    double dist = (double)lw_pathfinding_get_case_distance(target_cell, current_cell);
    /* Previous formula
     * return 0.5 + (area.getRadius() - dist) / area.getRadius() * 0.5;
     */
    return 1 - dist * 0.2;
}


/* public EffectParameters getEffectParametersByType(int type) */
const LwEffectParameters* lw_attack_get_effect_parameters_by_type(const LwAttack *self, int type) {
    for (int i = 0; i < self->n_effects; i++) {
        if (self->effects[i].id == type) {
            return &self->effects[i];
        }
    }
    return NULL;
}


/* public boolean needsEmptyCell() {
 *     for (EffectParameters ep : effects) {
 *         if (ep.getId() == Effect.TYPE_TELEPORT || ep.getId() == Effect.TYPE_SUMMON
 *             || ep.getId() == Effect.TYPE_RESURRECT)
 *             return true;
 *     }
 *     return false;
 * }
 */
int lw_attack_needs_empty_cell(const LwAttack *self) {
    for (int i = 0; i < self->n_effects; i++) {
        int id = self->effects[i].id;
        if (id == LW_EFFECT_TYPE_TELEPORT
            || id == LW_EFFECT_TYPE_SUMMON
            || id == LW_EFFECT_TYPE_RESURRECT) {
            return 1;
        }
    }
    return 0;
}
