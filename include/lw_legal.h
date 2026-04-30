/*
 * lw_legal.h -- enumerate legal actions for an entity.
 *
 * Mirrors the AI-side legal_actions(...) we built in Python, with
 * the same BFS-bounded move enumeration, range-pre-filter for
 * weapon/chip usage, and per-call LoS cache.
 *
 * The caller supplies:
 *   - the LwAttack profile for each weapon and chip in the entity's
 *     inventory (resolved against the engine's catalog at the Python /
 *     scenario layer; the C engine itself doesn't load the catalog).
 *   - chip cooldown state (read from entity.chip_cooldown).
 *
 * Output: an array of LwAction filled in the same order as
 * Python's legal_actions: END first, then SET_WEAPON, then
 * USE_WEAPON, then USE_CHIP, then MOVE.
 */
#ifndef LW_LEGAL_H
#define LW_LEGAL_H

#include "lw_state.h"
#include "lw_action.h"
#include "lw_attack.h"

/*
 * Catalog hooks: the AI / scenario passes parallel arrays of LwAttack
 * descriptors keyed by (entity_index, weapon_slot) and (entity_index,
 * chip_slot). For now we accept the descriptors per-entity for the
 * single entity being enumerated.
 */
typedef struct {
    int        weapon_costs[LW_MAX_INVENTORY];
    LwAttack   weapon_attacks[LW_MAX_INVENTORY];
    int        chip_costs[LW_MAX_INVENTORY];
    LwAttack   chip_attacks[LW_MAX_INVENTORY];
} LwInventoryProfile;


int lw_legal_actions(const LwState *state,
                     int entity_index,
                     const LwInventoryProfile *profile,
                     LwAction *out_actions,
                     int max_out);

#endif /* LW_LEGAL_H */
