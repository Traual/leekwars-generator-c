/*
 * lw_types.h -- core sizing constants and primitive typedefs.
 *
 * Pinned to match the Python reference engine. These values
 * become hard limits on State/Map/Entity allocations; raise
 * them only with care since memory layouts depend on them.
 */
#ifndef LW_TYPES_H
#define LW_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Hard limits ------------------------------------------------------ */

/* Maps ship by Leek Wars are at most ~613 cells (17x17 hex variants).
 * 700 leaves headroom. */
#define LW_MAX_CELLS         700

/* Map dimensions in cells: width x height. We pre-size the coord LUT
 * to 64x64 which covers the diamond bounding box of any official map. */
#define LW_COORD_DIM         64

/* Battle royale = 10 entities, each can summon 8 bulbs => 90 entities. */
#define LW_MAX_ENTITIES      90

/* Inventories cap at 6 (tunable from Python; engine just enumerates). */
#define LW_MAX_INVENTORY     6

/* Active effects on a single entity. Empirically <=12 even in worst
 * cases observed; keep some slack. */
#define LW_MAX_EFFECTS       16

/* Ignored cells for LoS + path queries -- worst case the line of sight
 * from corner to corner traverses ~25 cells. */
#define LW_MAX_LOS_IGNORED   8

/* A* path returned to caller: cap = max possible MP * 2 (we never need
 * more than the entity's MP). */
#define LW_MAX_PATH_LEN      32

/* Number of stat slots (matches engine STAT_LIFE..STAT_RAM, 18 with
 * gaps at 7,8). */
#define LW_STAT_COUNT        18

/* Entity state bitmask flags ---------------------------------------- */

#define LW_STATE_NONE         0u
#define LW_STATE_RESURRECTED  (1u << 1)
#define LW_STATE_UNHEALABLE   (1u << 2)
#define LW_STATE_INVINCIBLE   (1u << 3)
#define LW_STATE_PACIFIST     (1u << 4)
#define LW_STATE_HEAVY        (1u << 5)
#define LW_STATE_DENSE        (1u << 6)
#define LW_STATE_MAGNETIZED   (1u << 7)
#define LW_STATE_CHAINED      (1u << 8)
#define LW_STATE_ROOTED       (1u << 9)
#define LW_STATE_PETRIFIED    (1u << 10)
#define LW_STATE_STATIC       (1u << 11)

/* Stat indices -- must match engine.entity.Entity.STAT_* exactly.
 * Verified against leekwars/state/entity.py:15-34. The previous
 * version of this header had ABS/REL_SHIELD and RESISTANCE swapped
 * in numbering, which silently corrupted feature extraction and
 * damage shield arithmetic. */

#define LW_STAT_LIFE             0
#define LW_STAT_TP               1
#define LW_STAT_MP               2
#define LW_STAT_STRENGTH         3
#define LW_STAT_AGILITY          4
#define LW_STAT_FREQUENCY        5
#define LW_STAT_WISDOM           6
/* gaps at 7, 8 (unused by the engine) */
#define LW_STAT_ABSOLUTE_SHIELD  9
#define LW_STAT_RELATIVE_SHIELD 10
#define LW_STAT_RESISTANCE      11
#define LW_STAT_SCIENCE         12
#define LW_STAT_MAGIC           13
#define LW_STAT_DAMAGE_RETURN   14
#define LW_STAT_POWER           15
#define LW_STAT_CORES           16
#define LW_STAT_RAM             17

#endif /* LW_TYPES_H */
