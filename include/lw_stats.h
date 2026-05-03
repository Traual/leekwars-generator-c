/*
 * lw_stats.h -- 1:1 port of state/Stats.java
 *
 * Java:   public final Map<Integer, Integer> stats;
 * C:      Stats holds a fixed-size int[] indexed by stat key. Java's
 *         TreeMap<Integer,Integer> is replaced by a dense array because
 *         the keys (Entity.STAT_*) form a small contiguous range
 *         (0..LW_STAT_COUNT-1) and "missing key returns 0" is just
 *         "uninitialised slot is 0".  Iteration order in Java's TreeMap
 *         is by key ascending, which is what we get for free with an
 *         int[] indexed walk.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/Stats.java
 */
#ifndef LW_STATS_H
#define LW_STATS_H

#include "lw_constants.h"

typedef struct {
    int stats[LW_STAT_COUNT];
    /* Java's TreeMap also tracks "is key present" -- here, an entry is
     * "present" iff stats[k] != 0, which matches getStat()'s
     * "null -> 0" semantic. */
} LwStats;


/* public Stats() { this.stats = new TreeMap<...>(); } */
static inline void lw_stats_init(LwStats *self) {
    for (int i = 0; i < LW_STAT_COUNT; i++) {
        self->stats[i] = 0;
    }
}

/* public Stats(Stats stats) { this.stats = new TreeMap<...>(stats.stats); } */
static inline void lw_stats_copy(LwStats *self, const LwStats *src) {
    for (int i = 0; i < LW_STAT_COUNT; i++) {
        self->stats[i] = src->stats[i];
    }
}

/* public int getStat(int stat) {
 *     Integer retour = stats.get(stat);
 *     if (retour == null) return 0;
 *     return retour;
 * }
 */
static inline int lw_stats_get_stat(const LwStats *self, int stat) {
    if (stat < 0 || stat >= LW_STAT_COUNT) return 0;
    return self->stats[stat];
}

/* public void setStat(int key, int value) { stats.put(key, value); } */
static inline void lw_stats_set_stat(LwStats *self, int key, int value) {
    if (key < 0 || key >= LW_STAT_COUNT) return;
    self->stats[key] = value;
}

/* public void clear() { stats.clear(); } */
static inline void lw_stats_clear(LwStats *self) {
    for (int i = 0; i < LW_STAT_COUNT; i++) {
        self->stats[i] = 0;
    }
}

/* public void updateStat(int id, int delta) {
 *     stats.merge(id, delta, Integer::sum);
 * }
 * Note: TreeMap.merge(k, d, Integer::sum) creates entry with d if
 * missing, otherwise sets to old + d.  Both branches end at "stats[k] += d".
 */
static inline void lw_stats_update_stat(LwStats *self, int id, int delta) {
    if (id < 0 || id >= LW_STAT_COUNT) return;
    self->stats[id] += delta;
}

/* public void addStats(Stats to_add) {
 *     for (Entry<...> entry : to_add.stats.entrySet()) {
 *         updateStat(entry.getKey(), entry.getValue());
 *     }
 * }
 */
static inline void lw_stats_add_stats(LwStats *self, const LwStats *to_add) {
    for (int i = 0; i < LW_STAT_COUNT; i++) {
        if (to_add->stats[i] != 0) {
            lw_stats_update_stat(self, i, to_add->stats[i]);
        }
    }
}


#endif /* LW_STATS_H */
