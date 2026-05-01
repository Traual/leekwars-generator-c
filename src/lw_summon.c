/*
 * lw_summon.c -- bulb-template registry + summon allocation.
 *
 * Mirrors Python's State.createSummon + BulbTemplate.createInvocation
 * + Order.addSummon byte-for-byte:
 *
 *   stat = int((min + floor((max - min) * c)) * multiplier)
 *   c = min(300, owner.level) / 300
 *   multiplier = 1.2 if critical else 1.0
 *
 * The summon is appended to entities[], placed on the target cell,
 * and inserted into initial_order right AFTER the caster.
 */

#include "lw_summon.h"
#include <math.h>
#include <string.h>


static LwBulbTemplate g_bulb_catalog[LW_BULB_CATALOG_MAX];
static int            g_bulb_count = 0;


int lw_bulb_register(int bulb_id, const LwBulbTemplate *tpl) {
    if (bulb_id < 0 || tpl == NULL) return -1;

    /* Replace if already present. */
    for (int i = 0; i < g_bulb_count; i++) {
        if (g_bulb_catalog[i].bulb_id == bulb_id) {
            g_bulb_catalog[i] = *tpl;
            g_bulb_catalog[i].bulb_id = bulb_id;
            return 0;
        }
    }
    if (g_bulb_count >= LW_BULB_CATALOG_MAX) return -1;
    g_bulb_catalog[g_bulb_count] = *tpl;
    g_bulb_catalog[g_bulb_count].bulb_id = bulb_id;
    g_bulb_count++;
    return 0;
}


const LwBulbTemplate* lw_bulb_get(int bulb_id) {
    if (bulb_id < 0) return NULL;
    for (int i = 0; i < g_bulb_count; i++) {
        if (g_bulb_catalog[i].bulb_id == bulb_id) {
            return &g_bulb_catalog[i];
        }
    }
    return NULL;
}


void lw_bulb_clear(void) {
    memset(g_bulb_catalog, 0, sizeof(g_bulb_catalog));
    g_bulb_count = 0;
}


/* int((base + floor((bonus - base) * coeff)) * multiplier).
 * All operations in floating point, with floor() matching Python's
 * math.floor (round toward -infinity). The outer int() is C's
 * truncation toward zero -- since multiplier > 0 and the inner
 * value is non-negative for sane inputs, truncation == floor here. */
static int bulb_stat_base(int base_v, int bonus, double coeff,
                          double multiplier) {
    double inner = (double)base_v + floor((double)(bonus - base_v) * coeff);
    return (int)(inner * multiplier);
}


int lw_apply_summon(LwState *state,
                    int caster_idx,
                    int dest_cell,
                    int bulb_id,
                    int level,
                    int critical) {
    if (state == NULL) return -1;
    if (caster_idx < 0 || caster_idx >= state->n_entities) return -1;
    if (dest_cell < 0 || dest_cell >= LW_MAX_CELLS) return -1;
    if (state->n_entities >= LW_MAX_ENTITIES) return -1;

    /* Topology / cell sanity (when topology is registered). */
    if (state->map.topo != NULL) {
        if (dest_cell >= state->map.topo->n_cells) return -1;
        if (!state->map.topo->cells[dest_cell].walkable) return -1;
    }
    if (state->map.entity_at_cell[dest_cell] >= 0) return -1;

    const LwBulbTemplate *tpl = lw_bulb_get(bulb_id);
    if (tpl == NULL) return -1;

    LwEntity *caster = &state->entities[caster_idx];
    int owner_level = caster->level > 0 ? caster->level : level;
    if (owner_level > 300) owner_level = 300;
    if (owner_level < 0)   owner_level = 0;

    double coeff      = (double)owner_level / 300.0;
    double multiplier = critical ? 1.2 : 1.0;

    int new_idx = state->n_entities;
    LwEntity *e = &state->entities[new_idx];
    memset(e, 0, sizeof(LwEntity));

    e->id = new_idx;
    /* Python uses fid = -nextEntityId for summons (negative to mark). */
    e->fid = -(new_idx + 1);
    e->team_id = caster->team_id;
    e->level = level;
    e->alive = 1;
    e->equipped_weapon = -1;
    e->cell_id = dest_cell;

    int life     = bulb_stat_base(tpl->min_life,       tpl->max_life,       coeff, multiplier);
    int strength = bulb_stat_base(tpl->min_strength,   tpl->max_strength,   coeff, multiplier);
    int wisdom   = bulb_stat_base(tpl->min_wisdom,     tpl->max_wisdom,     coeff, multiplier);
    int agility  = bulb_stat_base(tpl->min_agility,    tpl->max_agility,    coeff, multiplier);
    int resist   = bulb_stat_base(tpl->min_resistance, tpl->max_resistance, coeff, multiplier);
    int science  = bulb_stat_base(tpl->min_science,    tpl->max_science,    coeff, multiplier);
    int magic    = bulb_stat_base(tpl->min_magic,      tpl->max_magic,      coeff, multiplier);
    int tp       = bulb_stat_base(tpl->min_tp,         tpl->max_tp,         coeff, multiplier);
    int mp       = bulb_stat_base(tpl->min_mp,         tpl->max_mp,         coeff, multiplier);

    if (life < 1)     life = 1;
    e->hp = life; e->total_hp = life;
    e->base_stats[LW_STAT_LIFE]       = life;
    e->base_stats[LW_STAT_STRENGTH]   = strength;
    e->base_stats[LW_STAT_WISDOM]     = wisdom;
    e->base_stats[LW_STAT_AGILITY]    = agility;
    e->base_stats[LW_STAT_RESISTANCE] = resist;
    e->base_stats[LW_STAT_SCIENCE]    = science;
    e->base_stats[LW_STAT_MAGIC]      = magic;
    e->base_stats[LW_STAT_TP]         = tp;
    e->base_stats[LW_STAT_MP]         = mp;
    e->base_stats[LW_STAT_FREQUENCY]  = 1;  /* Python passes 1 to Bulb ctor */

    e->n_weapons = 0;
    e->n_chips = tpl->n_chips < LW_MAX_INVENTORY ? tpl->n_chips : LW_MAX_INVENTORY;
    for (int i = 0; i < e->n_chips; i++) {
        e->chips[i] = tpl->chip_ids[i];
        e->chip_cooldown[i] = 0;
    }

    state->n_entities++;
    state->map.entity_at_cell[dest_cell] = new_idx;

    /* Insert into initial_order RIGHT AFTER the caster. Python's
     * Order.addSummon(owner, invoc) does the same. */
    if (state->n_in_order < LW_MAX_ENTITIES) {
        int insert_pos = state->n_in_order;  /* default: append at end */
        for (int i = 0; i < state->n_in_order; i++) {
            if (state->initial_order[i] == caster_idx) {
                insert_pos = i + 1;
                break;
            }
        }
        /* Shift right. */
        for (int i = state->n_in_order; i > insert_pos; i--) {
            state->initial_order[i] = state->initial_order[i - 1];
        }
        state->initial_order[insert_pos] = new_idx;
        state->n_in_order++;
        /* If we inserted at-or-before the current order_index, bump
         * order_index so we don't re-fire someone who already played. */
        if (insert_pos <= state->order_index) {
            state->order_index++;
        }
    }

    return new_idx;
}
