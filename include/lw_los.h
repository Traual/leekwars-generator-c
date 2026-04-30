/*
 * lw_los.h -- Line-of-sight + canUseAttack.
 *
 * Ports the algorithm from the Python verifyLoS at
 * leekwars/maps/map.py (and the Cython kernel _fast/_los.pyx).
 * We don't handle the FIRST_IN_LINE area pre-pass here; the wrapper
 * caller is expected to compute the ignored cell list (typically
 * just the start cell + optionally the first-entity-in-line).
 */
#ifndef LW_LOS_H
#define LW_LOS_H

#include "lw_types.h"
#include "lw_map.h"
#include "lw_attack.h"

/*
 * Verify line of sight from start_id to end_id.
 *
 * ``ignored_ids`` -- list of cell ids to skip blocking checks on.
 *                    Always includes start; may include first entity
 *                    on the line for FIRST_IN_LINE area attacks.
 * ``n_ignored``   -- number of valid entries.
 * ``need_los``    -- if 0 (false), returns 1 unconditionally.
 *
 * Returns 1 if LoS clear, 0 otherwise.
 */
int lw_verify_los(const LwMap *map,
                  int start_id,
                  int end_id,
                  const int *ignored_ids,
                  int  n_ignored,
                  int  need_los);

/*
 * verifyRange: Manhattan distance + launch-type filter.
 * Returns 1 if attack can target the cell range-wise; 0 otherwise.
 */
int lw_verify_range(const LwMap *map,
                    int start_id,
                    int end_id,
                    const LwAttack *attack);

/*
 * canUseAttack: verify_range + verify_los.
 */
int lw_can_use_attack(const LwMap *map,
                      int start_id,
                      int end_id,
                      const LwAttack *attack);

#endif /* LW_LOS_H */
