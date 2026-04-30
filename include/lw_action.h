/*
 * lw_action.h -- Action descriptors + apply / enumerate API.
 *
 * Five action types the AI can issue per entity per turn:
 *   END         -- stop acting this turn
 *   SET_WEAPON  -- change equipped weapon (cost 1 TP)
 *   MOVE        -- walk a path (cost = path length MP)
 *   USE_WEAPON  -- fire equipped weapon at a cell (cost weapon.cost TP)
 *   USE_CHIP    -- cast a chip at a cell (cost chip.cost TP, sets cooldown)
 *
 * apply_action mutates the State in place. legal_actions enumerates
 * candidate Actions for the current entity given its TP/MP/inventory
 * and the map state.
 *
 * USE_WEAPON / USE_CHIP delegate to the damage / effects system once
 * those land; for now they are stubs that just charge TP.
 */
#ifndef LW_ACTION_H
#define LW_ACTION_H

#include "lw_types.h"
#include "lw_state.h"
#include "lw_attack.h"

typedef enum {
    LW_ACTION_END        = 0,
    LW_ACTION_SET_WEAPON = 1,
    LW_ACTION_MOVE       = 2,
    LW_ACTION_USE_WEAPON = 3,
    LW_ACTION_USE_CHIP   = 4,
} LwActionType;

typedef struct {
    LwActionType type;

    /* Per-type parameters. Unused fields are 0 / -1. */
    int weapon_id;          /* SET_WEAPON: catalog id of weapon to equip */
    int chip_id;            /* USE_CHIP: catalog id of chip cast */
    int target_cell_id;     /* USE_WEAPON / USE_CHIP / MOVE end */
    int path[LW_MAX_PATH_LEN];
    int path_len;           /* MOVE: number of cells in path */
} LwAction;


/* Initialise an empty action with sensible default field values. */
static inline void lw_action_init(LwAction *a, LwActionType t) {
    a->type = t;
    a->weapon_id = -1;
    a->chip_id = -1;
    a->target_cell_id = -1;
    a->path_len = 0;
}


static inline int lw_action_is_terminal(const LwAction *a) {
    return a->type == LW_ACTION_END;
}


/*
 * Apply ``action`` to ``state`` for the entity at ``entity_index``
 * (index into state->entities, NOT FId). Returns 1 on success,
 * 0 if the action was malformed/illegal (no mutation in that case).
 */
int lw_apply_action(LwState *state,
                    int entity_index,
                    const LwAction *action);

#endif /* LW_ACTION_H */
