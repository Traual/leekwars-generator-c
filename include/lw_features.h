/*
 * lw_features.h -- feature extraction for the Leek-Zero V network
 *
 * Fills a flat float32 buffer with a fixed-size summary of the fight
 * state from the active entity's perspective. The MLP V-network
 * consumes this buffer directly.
 *
 * Design:
 *   16 entity slots × 16 features per slot = 256 floats.
 *   Slot 0 is always the active entity (the leek whose turn it is);
 *   slots 1..15 are filled by walking state.m_entities[] in
 *   insertion order, skipping the active entity. Empty slots are
 *   zero. The "is_me" / "is_my_team" / "is_alive" flags let the
 *   network distinguish the slots without needing a separate
 *   one-hot.
 *
 * Per-slot fields (16):
 *    0 is_alive         (0/1)
 *    1 is_me            (0/1)  -- this slot is the active entity
 *    2 is_my_team       (0/1)  -- same team as active entity
 *    3 hp_frac          life / total_life (0..1)
 *    4 tp_frac          (total_tp - used_tp) / total_tp (0..1)
 *    5 mp_frac          (total_mp - used_mp) / total_mp (0..1)
 *    6 life_norm        total_life / 5000
 *    7 strength_norm    strength / 500
 *    8 agility_norm     agility / 500
 *    9 wisdom_norm      wisdom / 500
 *   10 resistance_norm  resistance / 500
 *   11 magic_norm       magic / 500
 *   12 science_norm     science / 500
 *   13 frequency_norm   frequency / 500
 *   14 cell_x_norm      cell.x / 18 (or 0 if dead/no cell)
 *   15 cell_y_norm      cell.y / 18 (centered: (y + 17) / 35 -> 0..1)
 *
 * The same 16-feature schema is implementable in LeekScript using
 * the standard getEntity / getLife / getCell / etc. primitives, so
 * the trained weights will transfer 1:1 to a deployed AI.
 */
#ifndef LW_FEATURES_H
#define LW_FEATURES_H

#include "lw_state.h"

#define LW_FEAT_SLOTS              16
#define LW_FEAT_FIELDS_PER_SLOT    16
#define LW_FEAT_TOTAL              (LW_FEAT_SLOTS * LW_FEAT_FIELDS_PER_SLOT)   /* 256 */


/* Fill `out` (length >= LW_FEAT_TOTAL) with the feature vector for
 * the given active entity. Caller is responsible for allocating
 * `out` and zeroing it (the function only writes used slots).
 *
 * `active_idx` is an index into state->m_entities[]; if invalid (out
 * of range or dead), the active slot's features are written as a
 * dead/empty entity but the function still runs (returns 0).
 *
 * Returns the number of slots actually populated (1..16).
 */
int lw_features_extract_v(const LwState *state, int active_idx, float *out, int out_len);


#endif /* LW_FEATURES_H */
