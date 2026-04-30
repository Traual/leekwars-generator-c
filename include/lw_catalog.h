/*
 * lw_catalog.h -- weapon / chip attack-spec registry.
 *
 * The C engine doesn't load JSON itself; the Python or scenario layer
 * is expected to call lw_catalog_register at fight init for every
 * weapon and chip in play. After registration, lw_apply_action's
 * USE_WEAPON / USE_CHIP branches can look up the spec by item id and
 * route through lw_apply_attack_full for byte-for-byte execution.
 *
 * Thread safety: not safe for concurrent register + lookup. Populate
 * the catalog before any fight runs.
 *
 * Capacity: LW_CATALOG_MAX entries. Kept generous since the Leek
 * Wars catalog has ~100 items in total.
 */
#ifndef LW_CATALOG_H
#define LW_CATALOG_H

#include "lw_attack_apply.h"

#define LW_CATALOG_MAX  256

/* Register an attack spec for the given item id. Returns 0 on
 * success, -1 if the catalog is full or item_id is negative.
 * Re-registering an existing item id overwrites the previous spec. */
int lw_catalog_register(int item_id, const LwAttackSpec *spec);

/* Look up by item id. Returns NULL if unknown. */
const LwAttackSpec* lw_catalog_get(int item_id);

/* Wipe the catalog. Intended for tests; in production the catalog
 * is populated once and never cleared. */
void lw_catalog_clear(void);

/* Number of items currently registered. */
int  lw_catalog_size(void);

#endif /* LW_CATALOG_H */
