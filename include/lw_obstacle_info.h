/*
 * lw_obstacle_info.h -- 1:1 port of maps/ObstacleInfo.java
 *
 * Java:
 *   public class ObstacleInfo {
 *       private static HashMap<Integer, ObstacleInfo> obstacles = ...;
 *       public int size;
 *       public ObstacleInfo(int size) { this.size = size; }
 *       public static ObstacleInfo get(int id) { return obstacles.get(id); }
 *       static { obstacles.put(5, new ObstacleInfo(1)); ... }
 *   }
 *
 * NOTE: Java's HashMap<Integer, ObstacleInfo> with bounded keys (max id
 * observed = 66) is replaced by a dense lookup array indexed by id.
 * Java's get() returns null for missing keys; we mirror that with
 * size == 0 (no real obstacle has size 0) and a NULL return from
 * lw_obstacle_info_get(). Callers should check the return.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/ObstacleInfo.java
 */
#ifndef LW_OBSTACLE_INFO_H
#define LW_OBSTACLE_INFO_H

/* Highest obstacle id used in the Java static initializer is 66.
 * Use 67 so we can index by id directly. */
#define LW_OBSTACLE_INFO_MAX_ID   66
#define LW_OBSTACLE_INFO_TABLE_N  (LW_OBSTACLE_INFO_MAX_ID + 1)


typedef struct {
    int size;   /* public int size */
} LwObstacleInfo;


/* public static ObstacleInfo get(int id) {
 *     return obstacles.get(id);
 * }
 *
 * Returns NULL if id is out of range or no obstacle is registered for
 * that id (matches Java's HashMap.get returning null).
 */
const LwObstacleInfo* lw_obstacle_info_get(int id);


#endif /* LW_OBSTACLE_INFO_H */
