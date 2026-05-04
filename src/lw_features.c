/*
 * lw_features.c -- 256-d feature extractor for the V network.
 *
 * See include/lw_features.h for the slot/field schema. This file
 * intentionally does no heap allocation -- the caller owns the
 * output buffer and we just memset + write.
 */
#include "lw_features.h"

#include "lw_entity.h"
#include "lw_cell.h"

#include <string.h>


/* Normalisation constants -- chosen to land each stat roughly in
 * [0, 1] for typical fights. Mirror these exactly in the LeekScript
 * port so weights transfer. */
#define LW_FEAT_NORM_LIFE       5000.0f
#define LW_FEAT_NORM_STAT       500.0f
#define LW_FEAT_NORM_FREQ       500.0f
#define LW_FEAT_MAP_W           18.0f       /* logical map width */
#define LW_FEAT_Y_OFFSET        17.0f       /* cy ranges roughly [-17, +17] */
#define LW_FEAT_Y_RANGE         35.0f       /* (-17 + 17 + 1) ~ 35 */


/* Write the 16 fields for one entity into `slot` (16 floats wide).
 * Pass entity = NULL for an empty slot (all zeros).
 *
 * Centralised here so the schema is defined in one place and stays
 * in lock-step with the docstring in lw_features.h.
 */
static void fill_slot(float *slot, const struct LwEntity *e,
                       int is_me, int is_my_team) {
    /* Empty / dead-entity slot: mostly zeros, just the flags. */
    if (!e || !lw_entity_is_alive(e)) {
        slot[0]  = (e && lw_entity_is_alive(e)) ? 1.0f : 0.0f;
        slot[1]  = (float) (is_me & 1);
        slot[2]  = (float) (is_my_team & 1);
        for (int i = 3; i < LW_FEAT_FIELDS_PER_SLOT; i++) slot[i] = 0.0f;
        return;
    }

    int total_life = lw_entity_get_total_life(e);
    int life       = lw_entity_get_life(e);
    int total_tp   = lw_entity_get_total_tp(e);
    int total_mp   = lw_entity_get_total_mp(e);
    int used_tp    = e->used_tp;
    int used_mp    = e->used_mp;

    float hp_frac = (total_life > 0) ? (float)life / (float)total_life : 0.0f;
    float tp_frac = (total_tp   > 0) ? (float)(total_tp - used_tp) / (float)total_tp : 0.0f;
    float mp_frac = (total_mp   > 0) ? (float)(total_mp - used_mp) / (float)total_mp : 0.0f;

    /* Cell coords. Java's Cell uses (x, y) where y is in [-17..17]
     * after the diamond tilt; normalize to [0, 1]. If the entity has
     * no cell (very rare; usually only mid-spawn), we fall back to
     * the diamond center. */
    float cell_x_norm = 0.5f;
    float cell_y_norm = 0.5f;
    const struct LwCell *cell = lw_entity_get_cell((struct LwEntity*)e);
    if (cell) {
        cell_x_norm = (float)cell->x / LW_FEAT_MAP_W;
        cell_y_norm = ((float)cell->y + LW_FEAT_Y_OFFSET) / LW_FEAT_Y_RANGE;
    }

    slot[0]  = 1.0f;                                                          /* is_alive */
    slot[1]  = (float)(is_me      & 1);                                       /* is_me */
    slot[2]  = (float)(is_my_team & 1);                                       /* is_my_team */
    slot[3]  = hp_frac;                                                       /* hp_frac */
    slot[4]  = tp_frac;                                                       /* tp_frac */
    slot[5]  = mp_frac;                                                       /* mp_frac */
    slot[6]  = (float)total_life / LW_FEAT_NORM_LIFE;                         /* life_norm */
    slot[7]  = (float)lw_entity_get_strength  (e) / LW_FEAT_NORM_STAT;        /* strength */
    slot[8]  = (float)lw_entity_get_agility   (e) / LW_FEAT_NORM_STAT;        /* agility */
    slot[9]  = (float)lw_entity_get_wisdom    (e) / LW_FEAT_NORM_STAT;        /* wisdom */
    slot[10] = (float)lw_entity_get_resistance(e) / LW_FEAT_NORM_STAT;        /* resistance */
    slot[11] = (float)lw_entity_get_magic     (e) / LW_FEAT_NORM_STAT;        /* magic */
    slot[12] = (float)lw_entity_get_science   (e) / LW_FEAT_NORM_STAT;        /* science */
    slot[13] = (float)lw_entity_get_frequency (e) / LW_FEAT_NORM_FREQ;        /* frequency */
    slot[14] = cell_x_norm;                                                   /* cell_x */
    slot[15] = cell_y_norm;                                                   /* cell_y */
}


int lw_features_extract_v(const LwState *state, int active_idx,
                            float *out, int out_len) {
    if (!state || !out) return 0;
    if (out_len < LW_FEAT_TOTAL) return 0;

    /* Zero the whole buffer first; empty slots stay zero (apart from
     * what fill_slot writes for the flags). */
    memset(out, 0, sizeof(float) * LW_FEAT_TOTAL);

    int n_ent = ((LwState*)state)->n_entities;
    if (n_ent <= 0) return 0;

    /* Identify the active entity's team. If active_idx is invalid, we
     * treat team = -1 so no slot gets is_my_team = 1 except slot 0
     * (which gets is_me = 1 even if dead). */
    struct LwEntity *active = NULL;
    int my_team = -2;
    if (active_idx >= 0 && active_idx < n_ent) {
        active = ((LwState*)state)->m_entities[active_idx];
        if (active) my_team = lw_entity_get_team(active);
    }

    /* Slot 0 = active entity. */
    fill_slot(out, active, /*is_me=*/1, /*is_my_team=*/1);

    /* Slots 1..15 = remaining entities in insertion order, skipping
     * the active one. */
    int slot = 1;
    for (int i = 0; i < n_ent && slot < LW_FEAT_SLOTS; i++) {
        if (i == active_idx) continue;
        struct LwEntity *e = ((LwState*)state)->m_entities[i];
        int is_my = (e && lw_entity_get_team(e) == my_team) ? 1 : 0;
        fill_slot(out + slot * LW_FEAT_FIELDS_PER_SLOT, e, /*is_me=*/0, is_my);
        slot++;
    }

    return slot;
}
