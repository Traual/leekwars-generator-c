/*
 * lw_state.h -- top-level State struct.
 *
 * Holds the entire mutable game state. A clone is a single memcpy of
 * this struct (the topology is shared via pointer, so the only copy
 * cost is the entity array + map.entity_at_cell).
 *
 * Sized at compile time so we can stack-allocate or pool freely.
 * sizeof(LwState) ~= 200 KB; clone ~ 5-15 us depending on hardware.
 */
#ifndef LW_STATE_H
#define LW_STATE_H

#include "lw_types.h"
#include "lw_entity.h"
#include "lw_map.h"

/* Scenario type, matches engine.state.State.TYPE_* */
#define LW_TYPE_SOLO    0
#define LW_TYPE_FARMER  1
#define LW_TYPE_TEAM    2
#define LW_TYPE_BR      3

typedef struct {
    /* Map: topology is a pointer to a shared LwTopology, occupancy
     * is local to this state. */
    LwMap    map;

    /* Entities — id-indexed, packed at the front. */
    LwEntity entities[LW_MAX_ENTITIES];
    int      n_entities;

    /* Lookup helpers (built at scenario load, copied on clone) */
    int      entity_id_by_fid[LW_MAX_ENTITIES];

    /* Turn order */
    int      initial_order[LW_MAX_ENTITIES];   /* entity ids in init order */
    int      n_in_order;
    int      order_index;                      /* current position in order */

    int      turn;
    int      max_turns;
    int      winner;            /* -1 = none yet, or team index */

    /* RNG (Java LCG state) */
    uint64_t rng_n;

    /* Scenario context */
    uint8_t  scenario_type;
    uint8_t  context;
    int      seed;
} LwState;

/* Allocation / clone -------------------------------------------------- */

LwState* lw_state_alloc(void);
void     lw_state_free(LwState *s);
void     lw_state_clone(LwState *dst, const LwState *src);

/* Drain the internal recycle pool. Optional; useful at shutdown to
 * make leak-checkers happy. */
void     lw_state_pool_drain(void);

#endif /* LW_STATE_H */
