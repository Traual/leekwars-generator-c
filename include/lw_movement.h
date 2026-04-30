/*
 * lw_movement.h -- Push / Pull / Attract / Repel / Teleport / Slide.
 *
 * Movement-altering effects don't fit the (value, formula) shape of
 * the rest of the catalog -- they reposition entities on the grid
 * instead of mutating stats or HP. We expose them as state mutators
 * that take target/destination indices and let the caller wire them
 * into the attack-application path (where Push and Attract are
 * triggered by their respective effect type ids).
 *
 * References:
 *   - leekwars/maps/map.py::getPushLastAvailableCell
 *   - leekwars/maps/map.py::getAttractLastAvailableCell
 *   - leekwars/state/state.py::slideEntity / teleportEntity
 *
 * STATIC entities ignore slide attempts (Push/Repel/Attract). Teleport
 * bypasses STATIC per the Python reference (the Graal note explicitly
 * leaves it that way).
 */
#ifndef LW_MOVEMENT_H
#define LW_MOVEMENT_H

#include "lw_state.h"

/*
 * Compute the destination cell for a push.
 *
 * Direction is caster_cell -> entity_cell extended toward target_cell.
 * The entity slides one unit step at a time; stops at the first
 * non-available cell, or when target_cell is reached.
 *
 * Returns the destination cell id (==entity_cell if the geometry
 * doesn't form a valid push, or -1 if any input is bad).
 *
 * Mirrors getPushLastAvailableCell byte-for-byte: the loop walks
 * unit steps from the entity AWAY from the caster.
 */
int lw_compute_push_dest(const LwState *state,
                         int entity_cell, int target_cell, int caster_cell);

/*
 * Compute the destination cell for an attract.
 *
 * Direction is reversed -- the entity moves TOWARD the caster. The
 * push/attract direction-mismatch guard returns entity_cell unchanged.
 */
int lw_compute_attract_dest(const LwState *state,
                            int entity_cell, int target_cell, int caster_cell);

/*
 * Slide an entity to a destination cell. STATIC -> no-op. If
 * dest == entity's current cell (already there), no-op. Updates
 * map.entity_at_cell and entity.cell_id atomically.
 *
 * Returns 1 if the entity actually moved, 0 otherwise.
 */
int lw_apply_slide(LwState *state, int entity_idx, int dest_cell);

/*
 * Teleport an entity. Bypasses STATIC.
 *
 * Returns 1 if the entity moved (start != dest), 0 if same cell,
 * -1 on bad input.
 */
int lw_apply_teleport(LwState *state, int entity_idx, int dest_cell);

/*
 * Permutation: swap two entities' cells. Mirrors invertEntities.
 * Both entities must be alive AND not STATIC.
 *
 * Returns 1 if swapped, 0 otherwise.
 */
int lw_apply_permutation(LwState *state, int caster_idx, int target_idx);

#endif /* LW_MOVEMENT_H */
