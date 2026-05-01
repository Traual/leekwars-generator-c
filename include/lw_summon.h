/*
 * lw_summon.h -- bulb-template registry + summon entry point.
 *
 * Mirrors leekwars/bulbs/bulb_template.py + State.summonEntity /
 * createSummon. The C engine doesn't load bulb JSON itself; the
 * Python (or scenario) layer is expected to register each bulb
 * template via lw_bulb_register at fight init. Then the apply path
 * fires lw_apply_summon when a chip with TYPE_SUMMON is used.
 *
 * Bulb stats are computed from min..max ranges interpolated by the
 * caster's level (capped at 300):
 *
 *   c = min(300, owner.level) / 300
 *   multiplier = 1.2 if critical else 1.0
 *   stat = int((min + floor((max - min) * c)) * multiplier)
 *
 * Same formula as Python's BulbTemplate.base().
 */
#ifndef LW_SUMMON_H
#define LW_SUMMON_H

#include "lw_state.h"

#define LW_BULB_CATALOG_MAX  64

typedef struct {
    int  bulb_id;
    int  min_life,       max_life;
    int  min_strength,   max_strength;
    int  min_wisdom,     max_wisdom;
    int  min_agility,    max_agility;
    int  min_resistance, max_resistance;
    int  min_science,    max_science;
    int  min_magic,      max_magic;
    int  min_tp,         max_tp;
    int  min_mp,         max_mp;
    int  chip_ids[LW_MAX_INVENTORY];
    int  n_chips;
} LwBulbTemplate;

/* Register a template by id (replaces any existing). Returns 0 on
 * success, -1 if the catalog is full or input is invalid. */
int lw_bulb_register(int bulb_id, const LwBulbTemplate *tpl);

/* Look up by id; NULL if unknown. */
const LwBulbTemplate* lw_bulb_get(int bulb_id);

/* Wipe the catalog. */
void lw_bulb_clear(void);


/*
 * Spawn a bulb. Allocates the next free slot in state.entities[],
 * computes stats from the template + caster level + critical,
 * places at ``dest_cell``, appends to state.initial_order so the
 * bulb takes a turn. Returns the new entity index, or -1 on
 * failure (catalog full, slot full, dest_cell occupied, no
 * template, etc.).
 *
 * Insertion-order note: Python's State.createSummon uses
 * Order.addSummon(owner, invoc) which inserts the summon right
 * AFTER the owner in the turn order. We mirror that by walking
 * state.initial_order to find owner_idx, then shifting entries
 * right by one and inserting at position+1.
 */
int lw_apply_summon(LwState *state,
                    int caster_idx,
                    int dest_cell,
                    int bulb_id,
                    int level,
                    int critical);

#endif /* LW_SUMMON_H */
