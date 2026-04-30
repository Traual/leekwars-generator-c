/* lw_state.c — State alloc / free / clone (memcpy). */

#include "lw_state.h"
#include <stdlib.h>
#include <string.h>


LwState* lw_state_alloc(void) {
    LwState *s = (LwState*)calloc(1, sizeof(LwState));
    if (s == NULL) {
        return NULL;
    }
    /* Sentinel values for empty slots */
    s->n_entities = 0;
    s->n_in_order = 0;
    s->order_index = 0;
    s->turn = 0;
    s->max_turns = 64;
    s->winner = -1;
    s->rng_n = 0;
    s->scenario_type = LW_TYPE_SOLO;
    s->context = 0;
    s->seed = 0;
    s->map.topo = NULL;
    /* entity_at_cell init to -1 (calloc zeroed it; remap to -1 by hand) */
    for (int i = 0; i < LW_MAX_CELLS; i++) {
        s->map.entity_at_cell[i] = -1;
    }
    for (int i = 0; i < LW_MAX_ENTITIES; i++) {
        s->entity_id_by_fid[i] = -1;
        s->initial_order[i] = -1;
    }
    return s;
}

void lw_state_free(LwState *s) {
    if (s != NULL) {
        free(s);
    }
}

/*
 * Clone: pure memcpy. The Map's topology pointer is copied as a pointer
 * (shared, immutable), so all the static cell/coord/neighbors data is
 * shared between source and clone. Only entity_at_cell is copied as bytes.
 */
void lw_state_clone(LwState *dst, const LwState *src) {
    memcpy(dst, src, sizeof(LwState));
    /* topo pointer was copied by the memcpy; same shared topology. */
}
