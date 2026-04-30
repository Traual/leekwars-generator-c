/*
 * lw_entity.h -- Entity struct (a fighter / Leek / bulb).
 *
 * Layout designed for fast memcpy-based clone: every field is a value
 * type, no pointers. Stats are stored as arrays (base + buff) so the
 * combined value is base[k] + buff[k].
 */
#ifndef LW_ENTITY_H
#define LW_ENTITY_H

#include "lw_types.h"
#include "lw_effect.h"

typedef struct {
    /* Identity */
    int     id;
    int     fid;                /* fight-local id (used by AI) */
    int     team_id;
    int     farmer_id;
    int     level;

    /* Combat state */
    int     hp;                 /* current life */
    int     total_hp;
    int     used_tp;
    int     used_mp;

    /* Position */
    int     cell_id;            /* -1 if dead/no-cell */

    /* Inventory (catalog ids; resolved against item tables at runtime) */
    int     weapons[LW_MAX_INVENTORY];
    int     n_weapons;
    int     chips[LW_MAX_INVENTORY];
    int     n_chips;
    int     equipped_weapon;    /* index into weapons[] or -1 */

    /* Cooldowns -- one per chip slot */
    int     chip_cooldown[LW_MAX_INVENTORY];

    /* Stats (additive: combined = base + buff) */
    int     base_stats[LW_STAT_COUNT];
    int     buff_stats[LW_STAT_COUNT];

    /* Active effects */
    LwEffect effects[LW_MAX_EFFECTS];
    int      n_effects;

    /* Status flags (bitmask of LW_STATE_*) */
    uint32_t state_flags;

    /* Liveness */
    uint8_t  alive;
} LwEntity;

#endif /* LW_ENTITY_H */
