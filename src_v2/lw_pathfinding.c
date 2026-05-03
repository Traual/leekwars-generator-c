/*
 * lw_pathfinding.c -- 1:1 port of maps/Pathfinding.java
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Pathfinding.java
 */
#include "lw_pathfinding.h"

#include <stdlib.h>   /* abs */


/* Java: public final static byte NORTH = 0; // NE
 *       public final static byte EAST  = 1; // SE
 *       public final static byte SOUTH = 2; // SO
 *       public final static byte WEST  = 3; // NO
 *
 * Defined in lw_constants.h as LW_DIR_NORTH/EAST/SOUTH/WEST.
 *
 * Java: private final static boolean DEBUG = false;
 * Dead code -- only referenced inside the commented-out A* block.
 * Not ported.
 */


/* public static boolean inLine(Cell c1, Cell c2) {
 *     return c1.getX() == c2.getX() || c1.getY() == c2.getY();
 * }
 */
int lw_pathfinding_in_line(const LwCell *c1, const LwCell *c2) {
    return (lw_cell_get_x(c1) == lw_cell_get_x(c2)
         || lw_cell_get_y(c1) == lw_cell_get_y(c2)) ? 1 : 0;
}


/* public static int getAverageDistance2(Cell c1, List<Cell> cells) {
 *     int dist = 0;
 *     for (Cell c2 : cells) {
 *         dist += (c1.getX() - c2.getX()) * (c1.getX() - c2.getX())
 *               + (c1.getY() - c2.getY()) * (c1.getY() - c2.getY());
 *     }
 *     return dist / cells.size();
 * }
 */
int lw_pathfinding_get_average_distance2(const LwCell *c1,
                                          const LwCell *const *cells,
                                          int n_cells) {
    int dist = 0;
    for (int i = 0; i < n_cells; i++) {
        const LwCell *c2 = cells[i];
        dist += (lw_cell_get_x(c1) - lw_cell_get_x(c2)) * (lw_cell_get_x(c1) - lw_cell_get_x(c2))
              + (lw_cell_get_y(c1) - lw_cell_get_y(c2)) * (lw_cell_get_y(c1) - lw_cell_get_y(c2));
    }
    /* NOTE: Java throws ArithmeticException on empty list; the engine
     * never calls this with an empty list, so we guard defensively. */
    if (n_cells == 0) return 0;
    return dist / n_cells;
}


/* public static int getCaseDistance(Cell c1, Cell c2) {
 *     return Math.abs(c1.getX() - c2.getX()) + Math.abs(c1.getY() - c2.getY());
 * }
 */
int lw_pathfinding_get_case_distance(const LwCell *c1, const LwCell *c2) {
    return abs(lw_cell_get_x(c1) - lw_cell_get_x(c2))
         + abs(lw_cell_get_y(c1) - lw_cell_get_y(c2));
}


/* public static int getCaseDistance(Cell c1, List<Cell> cells) {
 *     int dist = -1;
 *     for (Cell c2 : cells) {
 *         int d = Math.abs(c1.getX() - c2.getX()) + Math.abs(c1.getY() - c2.getY());
 *         if (dist == -1 || d < dist)
 *             dist = d;
 *     }
 *     return dist;
 * }
 */
int lw_pathfinding_get_case_distance_to_list(const LwCell *c1,
                                              const LwCell *const *cells,
                                              int n_cells) {
    int dist = -1;
    for (int i = 0; i < n_cells; i++) {
        const LwCell *c2 = cells[i];
        int d = abs(lw_cell_get_x(c1) - lw_cell_get_x(c2))
              + abs(lw_cell_get_y(c1) - lw_cell_get_y(c2));
        if (dist == -1 || d < dist)
            dist = d;
    }
    return dist;
}


/* /\* === Commented-out A* implementation =================================
 *
 * Java keeps `getOldAStarPath(Cell c1, List<Cell> endCells, List<Cell>
 * cells_to_ignore)` commented out in source. Not ported -- the active
 * engine doesn't call it. If/when re-enabled, it will need:
 *   - Map.getCellsAround(Cell)            -> lw_map_get_cells_around
 *   - Map.getCellByDir(Cell, byte dir)    -> lw_map_get_cell_by_dir
 *   - Cell.available(List<Cell> ignore)   -> static helper here
 * ============================================================ *\/
 */


/* private static class Node {
 *     private final Cell cell;
 *     private Node parent;
 *     private final double distance;
 *     private int parcouru;
 *     private int poid;
 *
 *     public Node(Cell cell, double distance) {
 *         this.cell = cell;
 *         this.distance = (int) (distance);
 *         this.poid = (int) distance * 5;
 *     }
 *
 *     public void setParent(Node parent, int parcouru) {
 *         this.parcouru = parcouru;
 *         this.parent = parent;
 *         this.poid = (int) distance * 5 + parcouru;
 *     }
 *
 *     public Cell getCell()    { return cell;     }
 *     public int getParcouru() { return parcouru; }
 *     public int getPoid()     { return poid;     }
 *     public Node getParent()  { return parent;   }
 * }
 *
 * NOTE: Private inner class. Only referenced by the commented-out
 * getOldAStarPath above. Mirrored here as a static struct to keep
 * source order / grep-ability with the Java side; not currently used.
 * TODO(reactivate): wire to lw_pathfinding_old_astar_path() if/when
 * the Java commented block is re-enabled upstream.
 */
typedef struct LwPathfindingNode {
    const LwCell *cell;
    struct LwPathfindingNode *parent;
    double distance;
    int parcouru;
    int poid;
} LwPathfindingNode;

/* Suppress unused-type warning until the A* code is reactivated.
 * (sizeof reference; compiles to nothing.) */
static const int lw_pathfinding_node_size_marker
    = (int)sizeof(LwPathfindingNode);
