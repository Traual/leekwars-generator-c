/* lw_state.c -- State alloc / free / clone (memcpy).
 *
 * Includes a tiny LIFO free list so repeated alloc/free pairs in a
 * beam search don't pay calloc()'s ~100 us per round-trip. Beam clones
 * a state, scores it, throws it away -- thousands of times per turn.
 *
 * The pool is single-threaded (Python's GIL serialises calls). If we
 * ever go multithreaded the pool needs per-thread heads or a
 * lock-free stack; trivial change once needed.
 */

#include "lw_state.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>


/* ------------ free-list of recycled State structs ----------------- */

/* Per-process pool cap. 4096 was overkill (1 GB per worker × 8 workers
 * easily OOM'd on 16-32 GB boxes during beam-vs-beam training data
 * generation). Beam search peaks at ~200 live clones per turn, so 256
 * is enough to amortise the calloc cost without inflating RSS. */
#define LW_POOL_CAP  256

static LwState *g_pool[LW_POOL_CAP];
static int      g_pool_n = 0;

static LwState* pool_pop(void) {
    if (g_pool_n == 0) return NULL;
    return g_pool[--g_pool_n];
}

static int pool_push(LwState *s) {
    if (g_pool_n >= LW_POOL_CAP) return 0;
    g_pool[g_pool_n++] = s;
    return 1;
}


/* ------------ alloc -------------------------------------------------- */

static void state_init_defaults(LwState *s) {
    s->stream.enabled = 0;
    s->stream.n = 0;
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
    for (int i = 0; i < LW_MAX_CELLS; i++) {
        s->map.entity_at_cell[i] = -1;
    }
    for (int i = 0; i < LW_MAX_ENTITIES; i++) {
        s->entity_id_by_fid[i] = -1;
        s->initial_order[i] = -1;
    }
}


LwState* lw_state_alloc(void) {
    /* Try the pool first -- recycled buffers skip the calloc. */
    LwState *s = pool_pop();
    if (s != NULL) {
        /* The caller (typically Python's State() constructor) will
         * either init from scratch or memcpy a clone into us; either
         * way we reset the bookkeeping fields so a stale state can't
         * leak. The expensive arrays (entity_at_cell, entities[]) are
         * about to be overwritten by clone anyway. */
        state_init_defaults(s);
        return s;
    }
    s = (LwState*)calloc(1, sizeof(LwState));
    if (s == NULL) {
        return NULL;
    }
    state_init_defaults(s);
    return s;
}


void lw_state_free(LwState *s) {
    if (s == NULL) return;
    /* Push to pool if there's room; otherwise just free. The pool
     * holds at most LW_POOL_CAP * sizeof(LwState) ~= 800 MB which is
     * a hard ceiling, but in practice the AI's beam keeps only ~30
     * live clones at any time so the pool stays small. */
    if (!pool_push(s)) {
        free(s);
    }
}


/*
 * Clone: copy only the bytes we need to. The Map's topology pointer
 * is copied as a pointer (shared, immutable). The entities array is
 * sized for 90 (BR cap) but most fights use 2-8 slots, so we copy
 * only [0..n_entities) and leave the rest untouched on the destination.
 *
 * Layout-aware: we copy three contiguous regions instead of one big
 * memcpy of sizeof(LwState):
 *   1. The map + n_entities (everything before entities[])
 *   2. entities[0..n_entities)
 *   3. The trailing fields (entity_id_by_fid + initial_order + etc.)
 *
 * For a 4v4 farmer fight this drops the copy from ~245 KB to ~25 KB,
 * which is the difference between cache-line-resident and DRAM-bound.
 */
void lw_state_clone(LwState *dst, const LwState *src) {
    /* Region 1: map + n_entities (we need n_entities up-front to
     * know how much of entities[] to copy). */
    size_t prefix_bytes = offsetof(LwState, entities);
    memcpy(dst, src, prefix_bytes);

    /* Need to also copy the n_entities scalar that lives AFTER the
     * entities[] array. We do region 3 first to grab it, then come
     * back and copy the right entity slice. */
    size_t suffix_off = offsetof(LwState, n_entities);
    size_t suffix_bytes = sizeof(LwState) - suffix_off;
    memcpy((char*)dst + suffix_off, (const char*)src + suffix_off, suffix_bytes);

    int n = src->n_entities;
    if (n > 0 && n <= LW_MAX_ENTITIES) {
        memcpy(dst->entities, src->entities, (size_t)n * sizeof(LwEntity));
    }
}


/*
 * Drain and free the pool. Called at shutdown / between unrelated
 * scenarios so we don't carry stale buffers across.
 */
void lw_state_pool_drain(void) {
    while (g_pool_n > 0) {
        LwState *s = g_pool[--g_pool_n];
        free(s);
    }
}
