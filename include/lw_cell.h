/*
 * lw_cell.h -- 1:1 port of maps/Cell.java
 *
 * Java fields → C struct fields, same names where possible. Pathfinding
 * scratch fields (visited/closed/cost/weight/parent) live with the cell
 * in Java; we keep them there too so map snapshots roundtrip cleanly.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Cell.java
 */
#ifndef LW_CELL_H
#define LW_CELL_H

/* Forward decls -- the Map functions called by the Cell methods are
 * declared in lw_map.h; we only need the pointer type here. */
struct LwMap;
struct LwEntity;

typedef struct LwCell {
    int     id;            /* private final int id */

    int     walkable;      /* private boolean walkable -- 0/1 */
    int     obstacle;      /* private int obstacle */
    int     size;          /* private int size */

    int     north;         /* private boolean north = true */
    int     west;          /* private boolean west = true */
    int     east;          /* private boolean east = true */
    int     south;         /* private boolean south = true */

    int     x, y;          /* int x, y (package-private) */
    int     composante;    /* int composante */

    /* Pathfinding scratch (Java has them on Cell directly). */
    int     visited;       /* boolean visited = false */
    int     closed;        /* boolean closed = false */
    short   cost;          /* short cost = 0 */
    float   weight;        /* float weight = 0 */
    struct LwCell *parent; /* Cell parent = null */
} LwCell;


/* public Cell(Map map, int id)
 *
 * The constructor needs Map.getWidth() / getHeight(); we factor it out
 * into a free function that takes the dims directly so that callers
 * (including Map's own constructor) don't need to round-trip through
 * the Map struct yet.  Same arithmetic, same field assignments.
 */
static inline void lw_cell_init(LwCell *self, int id, int map_width, int map_height) {
    self->id = id;
    self->walkable = 1;
    self->obstacle = 0;
    self->size = 0;
    self->north = 1; self->west = 1; self->east = 1; self->south = 1;
    self->composante = 0;
    self->visited = 0; self->closed = 0; self->cost = 0;
    self->weight = 0.0f; self->parent = 0;

    int x = id % (map_width * 2 - 1);
    int y = id / (map_width * 2 - 1);
    if (y == 0 && x < map_width) {
        self->north = 0;
        self->west = 0;
    } else if (y + 1 == map_height && x >= map_width) {
        self->east = 0;
        self->south = 0;
    }
    if (x == 0) {
        self->south = 0;
        self->west = 0;
    } else if (x + 1 == map_width) {
        self->north = 0;
        self->east = 0;
    }

    /* On calcule Y */
    self->y = y - x % map_width;
    self->x = (id - (map_width - 1) * self->y) / map_width;
}


/* public Cell(Cell cell) */
static inline void lw_cell_copy(LwCell *self, const LwCell *cell) {
    self->id = cell->id;
    self->x = cell->x;
    self->y = cell->y;
    self->walkable = cell->walkable;
    self->composante = cell->composante;
    self->obstacle = cell->obstacle;
    self->size = cell->size;
    self->north = cell->north;
    self->west = cell->west;
    self->south = cell->south;
    self->east = cell->east;
    /* Pathfinding scratch reset (matches Java which leaves it as-is on
     * copy ctor; the engine resets between searches). */
    self->visited = 0;
    self->closed = 0;
    self->cost = 0;
    self->weight = 0.0f;
    self->parent = 0;
}


/* Trivial getters / setters -- inlined where the Java source has them. */

static inline int lw_cell_has_north(const LwCell *self) { return self->north; }
static inline int lw_cell_has_south(const LwCell *self) { return self->south; }
static inline int lw_cell_has_west (const LwCell *self) { return self->west;  }
static inline int lw_cell_has_east (const LwCell *self) { return self->east;  }

static inline int lw_cell_is_walkable     (const LwCell *self) { return self->walkable; }
static inline int lw_cell_get_obstacle    (const LwCell *self) { return self->obstacle; }
static inline int lw_cell_get_obstacle_size(const LwCell *self){ return self->size;     }

static inline void lw_cell_set_walkable(LwCell *self, int walkable) {
    self->walkable = walkable ? 1 : 0;
}

/* public void setObstacle(int id, int size) {
 *     this.walkable = false;
 *     this.obstacle = id;
 *     this.size = size;
 * }
 */
static inline void lw_cell_set_obstacle(LwCell *self, int id, int size) {
    self->walkable = 0;
    self->obstacle = id;
    self->size = size;
}

static inline int lw_cell_get_id    (const LwCell *self) { return self->id; }
static inline int lw_cell_get_x     (const LwCell *self) { return self->x;  }
static inline int lw_cell_get_y     (const LwCell *self) { return self->y;  }
static inline int lw_cell_get_composante(const LwCell *self) { return self->composante; }

/* available(Map) and getPlayer(Map) and next(Map,dx,dy) need Map ops;
 * declared here, defined in lw_map.c (and possibly lw_cell.c). */
int  lw_cell_available (const LwCell *self, const struct LwMap *map);
struct LwEntity* lw_cell_get_player(const LwCell *self, const struct LwMap *map);
LwCell* lw_cell_next   (const LwCell *self, const struct LwMap *map, int dx, int dy);


#endif /* LW_CELL_H */
