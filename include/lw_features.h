/*
 * lw_features.h -- feature extraction for the AI's MLP scorer.
 *
 * Reads State directly (no Python attribute dereference) and writes
 * 256 floats into a caller-provided buffer. This is what the Python
 * AI calls per state evaluated during beam search; the buffer can
 * be a numpy view (zero-copy from Python's perspective).
 *
 * Layout matches ai/training/features.py extract_mlp:
 *   0..15    globals (turn, scenario_type one-hot, team aggregates)
 *   16..47   "me" 32-feature slot
 *   48..143  3 ally slots (32 each), sorted by FId
 *   144..239 3 enemy slots (32 each), sorted by distance to me
 *   240..255 tactical features (best damage, threat, LoS, ...)
 *
 * The Python features.py keeps its own implementation as a reference;
 * this is the fast path. We don't need byte-for-byte identical output
 * for AI training (different scorer is fine), but we keep the same
 * normalisation constants so a Python-trained NN can be deployed
 * here without re-training.
 */
#ifndef LW_FEATURES_H
#define LW_FEATURES_H

#include "lw_state.h"

#define LW_MLP_FEAT_DIM 256

/*
 * Fill ``out[0..LW_MLP_FEAT_DIM)`` with the MLP feature vector for
 * ``my_team``'s perspective. ``out`` must be at least 256 floats
 * (typically a numpy view backing a torch tensor).
 *
 * The "current entity" defaults to the first alive entity on
 * ``my_team`` (matches the Python fallback when state.getOrder()
 * isn't available).
 */
void lw_extract_mlp(const LwState *state, int my_team, float *out);

#endif /* LW_FEATURES_H */
