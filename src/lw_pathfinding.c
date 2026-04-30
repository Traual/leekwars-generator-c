/*
 * lw_pathfinding.c -- A* and BFS implementations.
 *
 * Both algorithms operate on the topology in LwMap, using
 * map->topo->neighbors for adjacency and map->entity_at_cell to
 * skip occupied cells (except the goal in A*).
 */

#include "lw_pathfinding.h"
#include <math.h>
#include <stdint.h>
#include <string.h>


/* --------------------------------------------------------- A* --------- */

/* Min-heap of (f, counter, cell_id) triples. f is float, counter is
 * a decreasing int64 used to enforce LIFO tie-breaking (matches the
 * Python LIFO TreeSet-with-negative-comparator behaviour).
 *
 * We size the heap statically; LW_MAX_CELLS is an upper bound on the
 * number of distinct cell ids, hence on the heap size.
 */
typedef struct {
    double  f;
    int64_t counter;    /* decreasing => later entries pop first when f ties */
    int     cell_id;
} HeapNode;


static void heap_swap(HeapNode *a, HeapNode *b) {
    HeapNode t = *a; *a = *b; *b = t;
}

/* Compare: lower f wins. On f tie, GREATER counter wins (LIFO). */
static int heap_less(const HeapNode *a, const HeapNode *b) {
    if (a->f != b->f) return a->f < b->f;
    return a->counter > b->counter;
}

static void heap_push(HeapNode *heap, int *n, HeapNode v) {
    int i = (*n)++;
    heap[i] = v;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (heap_less(&heap[i], &heap[p])) {
            heap_swap(&heap[i], &heap[p]);
            i = p;
        } else {
            break;
        }
    }
}

static HeapNode heap_pop(HeapNode *heap, int *n) {
    HeapNode top = heap[0];
    --(*n);
    if (*n > 0) {
        heap[0] = heap[*n];
        int i = 0;
        for (;;) {
            int l = 2 * i + 1;
            int r = 2 * i + 2;
            int best = i;
            if (l < *n && heap_less(&heap[l], &heap[best])) best = l;
            if (r < *n && heap_less(&heap[r], &heap[best])) best = r;
            if (best == i) break;
            heap_swap(&heap[i], &heap[best]);
            i = best;
        }
    }
    return top;
}


int lw_astar_path(const LwMap *map,
                  int start_id,
                  int end_id,
                  int *out_path,
                  int out_path_capacity) {
    if (map == NULL || map->topo == NULL || out_path == NULL) return 0;
    const LwTopology *topo = map->topo;
    if (start_id < 0 || start_id >= topo->n_cells) return 0;
    if (end_id   < 0 || end_id   >= topo->n_cells) return 0;
    if (start_id == end_id) return 0;

    /* Per-cell scratch.  cost[id] = g, parent[id] = predecessor. */
    static int    cost[LW_MAX_CELLS];
    static int    parent[LW_MAX_CELLS];
    static uint8_t closed[LW_MAX_CELLS];

    for (int i = 0; i < topo->n_cells; i++) {
        cost[i] = -1;
        parent[i] = -1;
        closed[i] = 0;
    }

    HeapNode heap[LW_MAX_CELLS * 4];   /* generous; pairs of (push/pop) */
    int heap_n = 0;
    int64_t counter = 0;

    cost[start_id] = 0;
    HeapNode start = { 0.0, counter--, start_id };
    heap_push(heap, &heap_n, start);

    const LwCell *end_cell = &topo->cells[end_id];
    double end_x = (double)end_cell->x;
    double end_y = (double)end_cell->y;

    while (heap_n > 0) {
        HeapNode top = heap_pop(heap, &heap_n);
        int u = top.cell_id;
        if (closed[u]) continue;
        closed[u] = 1;

        if (u == end_id) {
            /* Reconstruct path: walk parent chain from end back to start. */
            int len = cost[end_id];
            if (len <= 0 || len > out_path_capacity) return 0;
            int cur = end_id;
            for (int i = len - 1; i >= 0; i--) {
                out_path[i] = cur;
                cur = parent[cur];
            }
            /* Skip last cell if it's the start blocked by entity? Python
             * version pops the last if entity is at it -- but in our
             * construction the end is allowed even if occupied, since
             * the move-enum filter skips occupied destinations. */
            return len;
        }

        int u_cost = cost[u];
        int new_cost = u_cost + 1;

        const int *neigh = topo->neighbors[u];
        for (int k = 0; k < 4; k++) {
            int nb = neigh[k];
            if (nb < 0) continue;
            if (closed[nb]) continue;
            const LwCell *nc = &topo->cells[nb];
            if (!nc->walkable) continue;
            int occupant = map->entity_at_cell[nb];
            if (occupant >= 0 && nb != end_id) continue;

            int old = cost[nb];
            if (old < 0 || new_cost < old) {
                cost[nb] = new_cost;
                parent[nb] = u;
                if (old < 0) {
                    /* heuristic: euclidean distance to end */
                    double dx = (double)nc->x - end_x;
                    double dy = (double)nc->y - end_y;
                    double h = sqrt(dx * dx + dy * dy);
                    HeapNode hn = { (double)new_cost + h, counter--, nb };
                    heap_push(heap, &heap_n, hn);
                }
            }
        }
    }
    return 0;
}


/* --------------------------------------------------------- BFS -------- */

int lw_bfs_reachable(const LwMap *map,
                     int start_id,
                     int max_dist,
                     int *out_dest_ids,
                     int (*out_paths)[LW_MAX_PATH_LEN],
                     int *out_path_lens,
                     int max_dests) {
    if (map == NULL || map->topo == NULL) return 0;
    if (max_dist <= 0) return 0;
    const LwTopology *topo = map->topo;
    if (start_id < 0 || start_id >= topo->n_cells) return 0;

    static int  dist[LW_MAX_CELLS];
    static int  parent[LW_MAX_CELLS];
    static int  queue[LW_MAX_CELLS];

    for (int i = 0; i < topo->n_cells; i++) {
        dist[i] = -1;
        parent[i] = -1;
    }
    int q_head = 0, q_tail = 0;
    dist[start_id] = 0;
    queue[q_tail++] = start_id;

    while (q_head < q_tail) {
        int cur = queue[q_head++];
        int d = dist[cur];
        if (d >= max_dist) continue;

        const int *neigh = topo->neighbors[cur];
        for (int k = 0; k < 4; k++) {
            int nb = neigh[k];
            if (nb < 0) continue;
            if (dist[nb] >= 0) continue;
            const LwCell *nc = &topo->cells[nb];
            if (!nc->walkable) continue;
            if (map->entity_at_cell[nb] >= 0) continue;
            dist[nb] = d + 1;
            parent[nb] = cur;
            queue[q_tail++] = nb;
        }
    }

    /* Collect destinations + reconstruct paths. We yield in BFS order
     * (smaller distance first), which matches no specific Python order
     * but is deterministic. */
    int n_out = 0;
    for (int i = 0; i < topo->n_cells && n_out < max_dests; i++) {
        if (i == start_id) continue;
        int d = dist[i];
        if (d <= 0) continue;
        if (d > LW_MAX_PATH_LEN) continue;  /* defensive */
        out_dest_ids[n_out] = i;
        out_path_lens[n_out] = d;
        /* Walk parent chain from i back to start, fill path back-to-front. */
        int cur = i;
        for (int j = d - 1; j >= 0; j--) {
            out_paths[n_out][j] = cur;
            cur = parent[cur];
        }
        n_out++;
    }
    return n_out;
}
