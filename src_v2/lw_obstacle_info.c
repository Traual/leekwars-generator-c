/*
 * lw_obstacle_info.c -- 1:1 port of maps/ObstacleInfo.java
 *
 * The Java class has a static initializer block that populates a
 * HashMap<Integer, ObstacleInfo>. We mirror it with a dense array
 * indexed by id; the static initializer becomes a single
 * compile-time array literal, so first call observes the populated
 * table just like the Java JVM observes the post-init HashMap.
 *
 * Slots not assigned below are zero-initialized (size == 0), which
 * lw_obstacle_info_get() reports as "missing key" -> NULL.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/ObstacleInfo.java
 */
#include "lw_obstacle_info.h"

#include <stddef.h>


/* Java:
 *   static {
 *       obstacles.put(5,  new ObstacleInfo(1));
 *       obstacles.put(20, new ObstacleInfo(1));
 *       ...
 *   }
 *
 * Layout: obstacles[id].size is the obstacle's footprint (in cells per
 * side). 0 means "id is not a registered obstacle".
 *
 * Designated initializers below mirror the put() order in the Java
 * static block exactly.
 */
static const LwObstacleInfo obstacles[LW_OBSTACLE_INFO_TABLE_N] = {
    /* size 1 */
    [5]  = { .size = 1 },
    [20] = { .size = 1 },
    [21] = { .size = 1 },
    [22] = { .size = 1 },
    [38] = { .size = 1 },
    [40] = { .size = 1 },
    [41] = { .size = 1 },
    [42] = { .size = 1 },
    [48] = { .size = 1 },
    [50] = { .size = 1 },
    [63] = { .size = 1 },
    [66] = { .size = 1 },
    [53] = { .size = 1 },
    [55] = { .size = 1 },
    [57] = { .size = 1 },
    [59] = { .size = 1 },
    [62] = { .size = 1 },
    [32] = { .size = 1 },

    /* size 2 */
    [11] = { .size = 2 },
    [17] = { .size = 2 },
    [18] = { .size = 2 },
    [34] = { .size = 2 },
    [43] = { .size = 2 },
    [44] = { .size = 2 },
    [45] = { .size = 2 },
    [46] = { .size = 2 },
    [47] = { .size = 2 },
    [49] = { .size = 2 },
    [64] = { .size = 2 },
    [65] = { .size = 2 },
    [52] = { .size = 2 },
    [54] = { .size = 2 },
    [56] = { .size = 2 },
    [58] = { .size = 2 },
    [61] = { .size = 2 },
    /* NOTE: matches Java verbatim -- the static block reassigns id 31
     * to size 1 in the "size 2" group. Last write wins in HashMap.put,
     * so the final entry for 31 is size 1. */
    [31] = { .size = 1 },

    /* size 3 */
    [51] = { .size = 3 },

    /* size 4 */
    [39] = { .size = 4 },

    /* size 5 */
    [60] = { .size = 5 },
};


/* Java: public static ObstacleInfo get(int id) {
 *           return obstacles.get(id);
 *       }
 *
 * Returns NULL if id is out of range or unmapped (size == 0).
 */
const LwObstacleInfo* lw_obstacle_info_get(int id) {
    if (id < 0 || id >= LW_OBSTACLE_INFO_TABLE_N) {
        return NULL;
    }
    if (obstacles[id].size == 0) {
        return NULL;
    }
    return &obstacles[id];
}
