/*
 * lw_map.c -- 1:1 port of maps/Map.java
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/maps/Map.java
 *
 * Order of operations is sacred. Every random draw, list iteration, and
 * setEntity callback happens at the same code position as in the Java
 * source -- the byte-for-byte action-stream parity test depends on it.
 *
 * Direct field access on `state->rng_n` is used for RNG draws (matches
 * the v1 engine convention -- see include/lw_state.h). The full LwState
 * struct lives in lw_state.h once that file is ported; until then this
 * file references the field through the forward-declared opaque type.
 */
#include "lw_map.h"
#include "lw_attack.h"  /* for lw_attack_get_min_range / get_max_range / get_launch_type / need_los */
#include "lw_pathfinding.h"
#include "lw_obstacle_info.h"
#include "lw_rng.h"
#include "lw_constants.h"

/* The full LwState struct (with rng_n) lives in lw_state.h, which is
 * being ported in a sibling task. We include it eagerly: until it
 * ships, lw_map.c won't compile in isolation, but the engine build
 * already pulls in lw_state.h before lw_map.c. */
#include "lw_state.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


/* ------------------------------------------------------------------ */
/* Java: public final static byte NORTH = 0;// NE                      */
/*       public final static byte EAST  = 1;// SE                      */
/*       public final static byte SOUTH = 2;// SO                      */
/*       public final static byte WEST  = 3;// NO                      */
/* private final static boolean DEBUG = false;                         */
/*                                                                     */
/* Already declared in lw_constants.h as LW_DIR_*. The DEBUG flag is   */
/* dead code in the parts of Map.java we port.                          */
/* ------------------------------------------------------------------ */


/* ============================================================== */
/* generateMap -- the start-of-fight RNG advance happens here.    */
/*                                                                  */
/* CRITICAL: every state.getRandom().getInt() call below must run  */
/* in the same order, with the same arguments, as the Java source. */
/* ============================================================== */

/* Forward declarations for static helpers used by generateMap below. */
static int  self_composante(const LwMap *self, int cell_id);


/* private void generate_map_set_obstacle_2sized(...): helper to
 * place a size-2 obstacle's "tail" cells, used by the custom-map
 * branch. Mirrors the inline block in the Java source. */
static void place_size2_tail(LwMap *map, LwCell *cell) {
    LwCell *c2 = lw_map_get_cell_by_dir(map, cell, LW_DIR_EAST);
    LwCell *c3 = lw_map_get_cell_by_dir(map, cell, LW_DIR_SOUTH);
    LwCell *c4 = (c3 != NULL) ? lw_map_get_cell_by_dir(map, c3, LW_DIR_EAST) : NULL;
    if (c2 != NULL) lw_cell_set_obstacle(c2, 0, -1);
    if (c3 != NULL) lw_cell_set_obstacle(c3, 0, -2);
    if (c4 != NULL) lw_cell_set_obstacle(c4, 0, -3);
}


/* private List<Cell> getCellsInCircle(Cell cell, int radius) {
 *     List<Cell> cells = new ArrayList<>();
 *     for (int x = cell.x - radius; x <= cell.x + radius; ++x) {
 *         for (int y = cell.y - radius; y <= cell.y + radius; ++y) {
 *             Cell c = getCell(x, y);
 *             if (c != null) cells.add(c);
 *         }
 *     }
 *     return cells;
 * }
 */
static int get_cells_in_circle(LwMap *self, const LwCell *cell, int radius,
                                LwCell **out_buf, int out_cap) {
    int n = 0;
    for (int x = cell->x - radius; x <= cell->x + radius; ++x) {
        for (int y = cell->y - radius; y <= cell->y + radius; ++y) {
            LwCell *c = lw_map_get_cell_xy(self, x, y);
            if (c != NULL) {
                if (n < out_cap) out_buf[n] = c;
                n++;
            }
        }
    }
    return n;
}


/* private void removeObstacle(Cell cell) {
 *     if (cell.getObstacleSize() > 0) {
 *         if (cell.getObstacleSize() == 2) {
 *             Cell c2 = getCellByDir(cell, Pathfinding.EAST);
 *             Cell c3 = getCellByDir(cell, Pathfinding.SOUTH);
 *             Cell c4 = getCellByDir(c3, Pathfinding.EAST);
 *             c2.setObstacle(0, 0); c2.setWalkable(true);
 *             c3.setObstacle(0, 0); c3.setWalkable(true);
 *             c4.setObstacle(0, 0); c4.setWalkable(true);
 *         }
 *         cell.setObstacle(0, 0);
 *         cell.setWalkable(true);
 *     }
 * }
 */
static void remove_obstacle(LwMap *self, LwCell *cell) {
    if (lw_cell_get_obstacle_size(cell) > 0) {
        if (lw_cell_get_obstacle_size(cell) == 2) {
            LwCell *c2 = lw_map_get_cell_by_dir(self, cell, LW_DIR_EAST);
            LwCell *c3 = lw_map_get_cell_by_dir(self, cell, LW_DIR_SOUTH);
            LwCell *c4 = (c3 != NULL) ? lw_map_get_cell_by_dir(self, c3, LW_DIR_EAST) : NULL;
            if (c2 != NULL) { lw_cell_set_obstacle(c2, 0, 0); lw_cell_set_walkable(c2, 1); }
            if (c3 != NULL) { lw_cell_set_obstacle(c3, 0, 0); lw_cell_set_walkable(c3, 1); }
            if (c4 != NULL) { lw_cell_set_obstacle(c4, 0, 0); lw_cell_set_walkable(c4, 1); }
        }
        lw_cell_set_obstacle(cell, 0, 0);
        lw_cell_set_walkable(cell, 1);
    }
}


/* public Map(int width, int height) {
 *     this(width, height, 0);
 * }
 */
void lw_map_init(LwMap *self, int width, int height) {
    lw_map_init_with_id(self, width, height, 0);
}


/* public Map(int width, int height, int id) {
 *
 *     this.width = width;
 *     this.height = height;
 *     this.id = id;
 *
 *     nb_cells = (width * 2 - 1) * height - (width - 1);
 *
 *     cells = new ArrayList<Cell>(nb_cells);
 *     for (int i = 0; i < nb_cells; i++) {
 *         Cell c = new Cell(this, i);
 *         cells.add(c);
 *         if (min_x == -1 || c.getX() < min_x) min_x = c.getX();
 *         if (max_x == -1 || c.getX() > max_x) max_x = c.getX();
 *         if (min_y == -1 || c.getY() < min_y) min_y = c.getY();
 *         if (max_y == -1 || c.getY() > max_y) max_y = c.getY();
 *     }
 *     int sx = max_x - min_x + 1;
 *     int sy = max_y - min_y + 1;
 *     coord = new Cell[sx][sy];
 *     for (int i = 0; i < nb_cells; i++) {
 *         Cell c = cells.get(i);
 *         coord[c.getX() - min_x][c.getY() - min_y] = c;
 *     }
 * }
 */
void lw_map_init_with_id(LwMap *self, int width, int height, int id) {

    self->width = width;
    self->height = height;
    self->id = id;

    self->type = 0;
    self->state = NULL;
    self->composantes = 0;
    self->pattern = 0;

    self->nb_cells = (width * 2 - 1) * height - (width - 1);
    self->max_cell_id = self->nb_cells - 1;

    /* Java: min_x = -1; ... max_y = -1; (instance-field init) */
    self->min_x = -1;
    self->max_x = -1;
    self->min_y = -1;
    self->max_y = -1;

    /* Initialise entity_at_cell[] = -1 (no occupant). */
    for (int i = 0; i < LW_MAP_MAX_CELLS; i++) {
        self->entity_at_cell[i] = -1;
    }

    /* Build cells[] and update min/max. */
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *c = &self->cells[i];
        lw_cell_init(c, i, width, height);
        if (self->min_x == -1 || c->x < self->min_x) self->min_x = c->x;
        if (self->max_x == -1 || c->x > self->max_x) self->max_x = c->x;
        if (self->min_y == -1 || c->y < self->min_y) self->min_y = c->y;
        if (self->max_y == -1 || c->y > self->max_y) self->max_y = c->y;
    }

    /* Initialise coord[][] to -1 (= null in Java). */
    for (int gx = 0; gx < LW_MAP_COORD_DIM; gx++) {
        for (int gy = 0; gy < LW_MAP_COORD_DIM; gy++) {
            self->coord[gx][gy] = -1;
        }
    }

    /* Java: coord = new Cell[sx][sy]; coord[x-min_x][y-min_y] = cell; */
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *c = &self->cells[i];
        self->coord[c->x - self->min_x][c->y - self->min_y] = c->id;
    }
}


/* public Map(Map map, State state) {
 *     this.id = map.id;
 *     this.width = map.width;
 *     ... (deep copy fields, remap entityByCell + cellByEntity)
 * }
 *
 * NOTE: Our struct is pure POD (no per-state pointers other than
 * `state`), so a single memcpy followed by `dst->state = new_state`
 * matches the Java semantics.
 */
void lw_map_clone(LwMap *dst, const LwMap *src, struct LwState *new_state) {
    memcpy(dst, src, sizeof(LwMap));
    dst->state = new_state;
}


/* public void setEntity(Entity entity, Cell cell) {
 *     this.entityByCell.put(cell, entity);
 *     this.cellByEntity.put(entity, cell);
 *     entity.setCell(cell);
 * }
 *
 * NOTE: cellByEntity is implicit -- LwEntity already holds its own
 * cell_id (lw_entity_set_cell), so we only need entity_at_cell[].
 */
void lw_map_set_entity(LwMap *self, struct LwEntity *e, LwCell *cell) {
    if (cell == NULL || e == NULL) return;
    /* Java: entityByCell.put(cell, entity); cellByEntity.put(entity, cell);
     * In C we key entity_at_cell by FID so it matches state.getEntity(fid). */
    self->entity_at_cell[cell->id] = lw_entity_get_fid(e);
    lw_entity_set_cell(e, cell);
}


/* public void moveEntity(Entity entity, Cell cell) {
 *     var oldCell = this.cellByEntity.remove(entity);
 *     this.entityByCell.remove(oldCell);
 *     this.entityByCell.put(cell, entity);
 *     this.cellByEntity.put(entity, cell);
 *     entity.setCell(cell);
 * }
 */
int lw_map_move_entity(LwMap *self, struct LwEntity *e, LwCell *cell) {
    if (self == NULL || e == NULL || cell == NULL) return 0;
    int old_cell_id = lw_entity_get_cell_id(e);
    if (old_cell_id >= 0 && old_cell_id < LW_MAP_MAX_CELLS) {
        self->entity_at_cell[old_cell_id] = -1;
    }
    /* Record the entity by its FID so map.entity_at_cell[]'s value
     * matches the FID-keyed lw_state_get_entity used elsewhere. */
    self->entity_at_cell[cell->id] = lw_entity_get_fid(e);
    lw_entity_set_cell(e, cell);
    return 1;
}


/* public void removeEntity(Entity entity) {
 *     var cell = this.cellByEntity.remove(entity);
 *     this.entityByCell.remove(cell);
 *     entity.setCell(null);
 * }
 */
void lw_map_remove_entity(LwMap *self, int entity_idx) {
    if (self->state == NULL) return;
    struct LwEntity *e = lw_state_get_entity(self->state, entity_idx);
    if (e == NULL) return;
    int old_cell_id = lw_entity_get_cell_id(e);
    if (old_cell_id >= 0 && old_cell_id < LW_MAP_MAX_CELLS) {
        self->entity_at_cell[old_cell_id] = -1;
    }
    lw_entity_set_cell(e, NULL);
}


/* public void invertEntities(Entity entity1, Entity entity2) {
 *     var cell1 = this.cellByEntity.get(entity1);
 *     var cell2 = this.cellByEntity.get(entity2);
 *     this.cellByEntity.put(entity1, cell2);
 *     this.cellByEntity.put(entity2, cell1);
 *     this.entityByCell.put(cell1, entity2);
 *     this.entityByCell.put(cell2, entity1);
 *     entity1.setCell(cell2);
 *     entity2.setCell(cell1);
 * }
 */
void lw_map_invert_entities(LwMap *self, struct LwEntity *e1, struct LwEntity *e2) {
    if (self == NULL || e1 == NULL || e2 == NULL) return;
    int cell1_id = lw_entity_get_cell_id(e1);
    int cell2_id = lw_entity_get_cell_id(e2);
    LwCell *cell1 = (cell1_id >= 0 && cell1_id < LW_MAP_MAX_CELLS) ? &self->cells[cell1_id] : NULL;
    LwCell *cell2 = (cell2_id >= 0 && cell2_id < LW_MAP_MAX_CELLS) ? &self->cells[cell2_id] : NULL;
    /* entity_at_cell stores FIDs (matches state.getEntity FID lookup). */
    if (cell1 != NULL) self->entity_at_cell[cell1->id] = lw_entity_get_fid(e2);
    if (cell2 != NULL) self->entity_at_cell[cell2->id] = lw_entity_get_fid(e1);
    lw_entity_set_cell(e1, cell2);
    lw_entity_set_cell(e2, cell1);
}


/* public Entity getEntity(Cell cell) { return this.entityByCell.get(cell); } */
int lw_map_get_entity(const LwMap *self, const LwCell *cell) {
    if (cell == NULL) return -1;
    if (cell->id < 0 || cell->id >= LW_MAP_MAX_CELLS) return -1;
    return self->entity_at_cell[cell->id];
}


/* public Cell getCell(int id) {
 *     if (id < 0 || id >= cells.size()) return null;
 *     return cells.get(id);
 * }
 */
LwCell* lw_map_get_cell(LwMap *self, int id) {
    if (id < 0 || id >= self->nb_cells) return NULL;
    return &self->cells[id];
}

const LwCell* lw_map_get_cell_const(const LwMap *self, int id) {
    if (id < 0 || id >= self->nb_cells) return NULL;
    return &self->cells[id];
}


/* public Cell getCell(int x, int y) {
 *     try {
 *         return coord[x - min_x][y - min_y];
 *     } catch (ArrayIndexOutOfBoundsException e) {
 *         return null;
 *     }
 * }
 */
LwCell* lw_map_get_cell_xy(LwMap *self, int x, int y) {
    int gx = x - self->min_x;
    int gy = y - self->min_y;
    if (gx < 0 || gy < 0) return NULL;
    if (gx >= LW_MAP_COORD_DIM || gy >= LW_MAP_COORD_DIM) return NULL;
    /* Java's coord[][] is sized exactly (max_x-min_x+1) x (max_y-min_y+1);
     * we additionally clamp to those dims to mirror the
     * ArrayIndexOutOfBoundsException catch. */
    if (gx > self->max_x - self->min_x) return NULL;
    if (gy > self->max_y - self->min_y) return NULL;
    int cell_id = self->coord[gx][gy];
    if (cell_id < 0) return NULL;
    return &self->cells[cell_id];
}

const LwCell* lw_map_get_cell_xy_const(const LwMap *self, int x, int y) {
    int gx = x - self->min_x;
    int gy = y - self->min_y;
    if (gx < 0 || gy < 0) return NULL;
    if (gx >= LW_MAP_COORD_DIM || gy >= LW_MAP_COORD_DIM) return NULL;
    if (gx > self->max_x - self->min_x) return NULL;
    if (gy > self->max_y - self->min_y) return NULL;
    int cell_id = self->coord[gx][gy];
    if (cell_id < 0) return NULL;
    return &self->cells[cell_id];
}


/* public Cell getNextCell(Cell cell, int dx, int dy) {
 *     var x = cell.x + dx;
 *     var y = cell.y + dy;
 *     if (x < this.min_x || y < this.min_y || x > this.max_x || y > this.max_y) {
 *         return null;
 *     }
 *     return this.coord[x - this.min_x][y - this.min_y];
 * }
 */
LwCell* lw_map_get_next_cell(LwMap *self, const LwCell *cell, int dx, int dy) {
    int x = cell->x + dx;
    int y = cell->y + dy;
    if (x < self->min_x || y < self->min_y || x > self->max_x || y > self->max_y) {
        return NULL;
    }
    int cell_id = self->coord[x - self->min_x][y - self->min_y];
    if (cell_id < 0) return NULL;
    return &self->cells[cell_id];
}


/* public Cell getCellByDir(Cell c, byte dir) {
 *     if (c == null) return null;
 *     if (dir == NORTH && c.hasNorth()) return getCell(c.getId() - width + 1);
 *     else if (dir == WEST && c.hasWest())  return getCell(c.getId() - width);
 *     else if (dir == EAST && c.hasEast())  return getCell(c.getId() + width);
 *     else if (dir == SOUTH && c.hasSouth())return getCell(c.getId() + width - 1);
 *     return null;
 * }
 */
LwCell* lw_map_get_cell_by_dir(LwMap *self, const LwCell *c, int dir) {
    if (c == NULL) return NULL;
    if (dir == LW_DIR_NORTH && lw_cell_has_north(c)) return lw_map_get_cell(self, c->id - self->width + 1);
    else if (dir == LW_DIR_WEST  && lw_cell_has_west (c)) return lw_map_get_cell(self, c->id - self->width);
    else if (dir == LW_DIR_EAST  && lw_cell_has_east (c)) return lw_map_get_cell(self, c->id + self->width);
    else if (dir == LW_DIR_SOUTH && lw_cell_has_south(c)) return lw_map_get_cell(self, c->id + self->width - 1);
    return NULL;
}


/* public Cell[] getCellsAround(Cell c) {
 *     return new Cell[] {
 *         getCellByDir(c, SOUTH),
 *         getCellByDir(c, WEST),
 *         getCellByDir(c, NORTH),
 *         getCellByDir(c, EAST)
 *     };
 * }
 */
void lw_map_get_cells_around(LwMap *self, const LwCell *c, LwCell *out[4]) {
    out[0] = lw_map_get_cell_by_dir(self, c, LW_DIR_SOUTH);
    out[1] = lw_map_get_cell_by_dir(self, c, LW_DIR_WEST);
    out[2] = lw_map_get_cell_by_dir(self, c, LW_DIR_NORTH);
    out[3] = lw_map_get_cell_by_dir(self, c, LW_DIR_EAST);
}


/* public void clear() {
 *     for (Cell c : cells) {
 *         c.setObstacle(0, 0);
 *         c.setWalkable(true);
 *     }
 * }
 */
void lw_map_clear(LwMap *self) {
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *c = &self->cells[i];
        lw_cell_set_obstacle(c, 0, 0);
        lw_cell_set_walkable(c, 1);
    }
}


/* public Cell getRandomCell(State state) {
 *     Cell retour = null;
 *     int nb = 0;
 *     while (retour == null || !retour.available(this)) {
 *         retour = getCell(state.getRandom().getInt(0, nb_cells));
 *         if (nb++ > 64) break;
 *     }
 *     return retour;
 * }
 */
LwCell* lw_map_get_random_cell(LwMap *self, struct LwState *state) {
    LwCell *retour = NULL;
    int nb = 0;
    while (retour == NULL || !lw_cell_available(retour, self)) {
        retour = lw_map_get_cell(self, lw_rng_get_int(&state->rng_n, 0, self->nb_cells));
        if (nb++ > 64) break;
    }
    return retour;
}


/* public Cell getRandomCell(State state, int part) {
 *     Cell retour = null;
 *     int nb = 0;
 *     while (retour == null || !retour.available(this)) {
 *         int y = state.getRandom().getInt(0, height - 1);
 *         int x = state.getRandom().getInt(0, width / 4);
 *         int cellid = y * (width * 2 - 1);
 *         cellid += (part - 1) * width / 4 + x;
 *         retour = getCell(cellid);
 *         if (nb++ > 64) break;
 *     }
 *     return retour;
 * }
 *
 * NOTE: 2 RNG draws per attempt (y then x). The byte-for-byte parity
 * test depends on this exact order. DO NOT collapse.
 */
LwCell* lw_map_get_random_cell_part(LwMap *self, struct LwState *state, int part) {
    LwCell *retour = NULL;
    int nb = 0;
    while (retour == NULL || !lw_cell_available(retour, self)) {
        int y = lw_rng_get_int(&state->rng_n, 0, self->height - 1);
        int x = lw_rng_get_int(&state->rng_n, 0, self->width / 4);
        int cellid = y * (self->width * 2 - 1);
        cellid += (part - 1) * self->width / 4 + x;
        retour = lw_map_get_cell(self, cellid);
        if (nb++ > 64) break;
    }
    return retour;
}


/* public Cell getCellEqualDistance(State state) {
 *     // Cellule à distance éguale des deux équipes
 *     var possible = new ArrayList<Cell>();
 *     for (var cell : cells) {
 *         if (cell.available(this) && Math.abs(getDistanceWithTeam(state, 0, cell) - getDistanceWithTeam(state, 1, cell)) < 2) {
 *             possible.add(cell);
 *         }
 *     }
 *     if (possible.size() > 0) {
 *         int i = state.getRandom().getInt(0, possible.size() - 1);
 *         return possible.get(i);
 *     }
 *     return getRandomCell(state);
 * }
 */
LwCell* lw_map_get_cell_equal_distance(LwMap *self, struct LwState *state) {
    /* Cellule à distance éguale des deux équipes */
    LwCell *possible[LW_MAP_MAX_CELLS];
    int n_possible = 0;
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *cell = &self->cells[i];
        if (lw_cell_available(cell, self) &&
            abs(lw_map_get_distance_with_team(self, state, 0, cell) -
                lw_map_get_distance_with_team(self, state, 1, cell)) < 2) {
            possible[n_possible++] = cell;
        }
    }
    if (n_possible > 0) {
        int i = lw_rng_get_int(&state->rng_n, 0, n_possible - 1);
        return possible[i];
    }
    return lw_map_get_random_cell(self, state);
}


/* public List<Cell> getCellsEqualDistance(Cell cell1, Cell cell2) {
 *     var result = new ArrayList<Cell>();
 *     for (var cell : cells) {
 *         if (cell.isWalkable() && Math.abs(Pathfinding.getCaseDistance(cell, cell1) - Pathfinding.getCaseDistance(cell, cell2)) < 2) {
 *             result.add(cell);
 *         }
 *     }
 *     return result;
 * }
 */
int lw_map_get_cells_equal_distance(LwMap *self,
                                    const LwCell *cell1,
                                    const LwCell *cell2,
                                    LwCell **out_buf, int out_cap) {
    int n = 0;
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *cell = &self->cells[i];
        if (lw_cell_is_walkable(cell) &&
            abs(lw_pathfinding_get_case_distance(cell, cell1) -
                lw_pathfinding_get_case_distance(cell, cell2)) < 2) {
            if (n < out_cap) out_buf[n] = cell;
            n++;
        }
    }
    return n;
}


/* public int getDistanceWithTeam(State state, int team, Cell cell) {
 *     int min = Integer.MAX_VALUE;
 *     for (var entity : state.getTeamEntities(team)) {
 *         int d = Pathfinding.getCaseDistance(entity.getCell(), cell);
 *         if (d < min) {
 *             min = d;
 *         }
 *     }
 *     return min;
 * }
 */
int lw_map_get_distance_with_team(LwMap *self, struct LwState *state, int team, const LwCell *cell) {
    int min = INT32_MAX;
    int n = lw_state_get_team_entity_count(state, team);
    for (int i = 0; i < n; i++) {
        int entity_idx = lw_state_get_team_entity(state, team, i);
        struct LwEntity *e = lw_state_get_entity(state, entity_idx);
        if (e == NULL) continue;
        int cell_id = lw_entity_get_cell_id(e);
        if (cell_id < 0) continue;
        const LwCell *ec = &self->cells[cell_id];
        int d = lw_pathfinding_get_case_distance(ec, cell);
        if (d < min) {
            min = d;
        }
    }
    return min;
}


/* public Cell getTeamBarycenter(State state, int team) {
 *     int tx = 0;
 *     int ty = 0;
 *     var entities = state.getTeamEntities(team);
 *     for (var entity : entities) {
 *         tx += entity.getCell().x;
 *         ty += entity.getCell().y;
 *     }
 *     return getCell(tx / entities.size(), ty / entities.size());
 * }
 */
LwCell* lw_map_get_team_barycenter(LwMap *self, struct LwState *state, int team) {
    int tx = 0;
    int ty = 0;
    int n = lw_state_get_team_entity_count(state, team);
    for (int i = 0; i < n; i++) {
        int entity_idx = lw_state_get_team_entity(state, team, i);
        struct LwEntity *e = lw_state_get_entity(state, entity_idx);
        if (e == NULL) continue;
        int cell_id = lw_entity_get_cell_id(e);
        if (cell_id < 0) continue;
        const LwCell *ec = &self->cells[cell_id];
        tx += ec->x;
        ty += ec->y;
    }
    if (n == 0) return NULL;
    return lw_map_get_cell_xy(self, tx / n, ty / n);
}


/* public Cell getRandomCellNearCenter(State state, int maxDistance) {
 *     Cell center = getCell(nb_cells / 2);
 *     var possible = new ArrayList<Cell>();
 *     for (var cell : cells) {
 *         if (cell.available(this) && Pathfinding.getCaseDistance(cell, center) <= maxDistance) {
 *             possible.add(cell);
 *         }
 *     }
 *     if (possible.size() > 0) {
 *         return possible.get(state.getRandom().getInt(0, possible.size() - 1));
 *     }
 *     return getRandomCell(state);
 * }
 */
LwCell* lw_map_get_random_cell_near_center(LwMap *self, struct LwState *state, int max_distance) {
    LwCell *center = lw_map_get_cell(self, self->nb_cells / 2);
    LwCell *possible[LW_MAP_MAX_CELLS];
    int n_possible = 0;
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *cell = &self->cells[i];
        if (lw_cell_available(cell, self) && lw_pathfinding_get_case_distance(cell, center) <= max_distance) {
            possible[n_possible++] = cell;
        }
    }
    if (n_possible > 0) {
        return possible[lw_rng_get_int(&state->rng_n, 0, n_possible - 1)];
    }
    return lw_map_get_random_cell(self, state);
}


/* public Cell getRandomCellAwayFromCenter(State state, int minDistance) {
 *     Cell center = getCell(nb_cells / 2);
 *     var possible = new ArrayList<Cell>();
 *     for (var cell : cells) {
 *         if (cell.available(this) && Pathfinding.getCaseDistance(cell, center) >= minDistance) {
 *             possible.add(cell);
 *         }
 *     }
 *     if (possible.size() > 0) {
 *         return possible.get(state.getRandom().getInt(0, possible.size() - 1));
 *     }
 *     return getRandomCell(state);
 * }
 */
LwCell* lw_map_get_random_cell_away_from_center(LwMap *self, struct LwState *state, int min_distance) {
    LwCell *center = lw_map_get_cell(self, self->nb_cells / 2);
    LwCell *possible[LW_MAP_MAX_CELLS];
    int n_possible = 0;
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *cell = &self->cells[i];
        if (lw_cell_available(cell, self) && lw_pathfinding_get_case_distance(cell, center) >= min_distance) {
            possible[n_possible++] = cell;
        }
    }
    if (n_possible > 0) {
        return possible[lw_rng_get_int(&state->rng_n, 0, n_possible - 1)];
    }
    return lw_map_get_random_cell(self, state);
}


/* public Cell getRandomCellAtDistance(Cell cell1, int distance) {
 *     var result = new ArrayList<Cell>();
 *     for (var cell : cells) {
 *         if (cell.isWalkable() && Pathfinding.getCaseDistance(cell, cell1) == distance) {
 *             result.add(cell);
 *         }
 *     }
 *     if (result.size() == 0) return null;
 *     return result.get((int) (result.size() * Math.random()));
 * }
 *
 * NOTE: Java uses Math.random() (the JVM-global PRNG) here, NOT the
 * deterministic engine RNG. We mirror that with lw_java_math_random()
 * so that test seeds keep their recorded output.
 */
LwCell* lw_map_get_random_cell_at_distance(LwMap *self, const LwCell *cell1, int distance) {
    LwCell *result[LW_MAP_MAX_CELLS];
    int n_result = 0;
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *cell = &self->cells[i];
        if (lw_cell_is_walkable(cell) && lw_pathfinding_get_case_distance(cell, cell1) == distance) {
            result[n_result++] = cell;
        }
    }
    if (n_result == 0) return NULL;
    return result[(int)((double)n_result * lw_java_math_random())];
}


/* public void computeComposantes() {
 *     var connexe = new int[this.coord.length][this.coord[0].length];
 *     int x, y, x2, y2, ni = 1;
 *     for (x = 0; x < connexe.length; x++) {
 *         for (y = 0; y < connexe[x].length; y++)
 *             connexe[x][y] = -1;
 *     }
 *
 *     // On cherche les composantes connexes
 *     for (x = 0; x < connexe.length; x++) {
 *         for (y = 0; y < connexe[x].length; y++) {
 *             Cell c = this.coord[x][y];
 *             if (c == null) continue;
 *             int cur_number = 0;
 *             if (x > 0 && this.coord[x - 1][y] != null && this.coord[x - 1][y].isWalkable() == c.isWalkable())
 *                 cur_number = connexe[x - 1][y];
 *
 *             if (y > 0 && this.coord[x][y - 1] != null && this.coord[x][y - 1].isWalkable() == c.isWalkable()) {
 *                 if (cur_number == 0)
 *                     cur_number = connexe[x][y - 1];
 *                 else if (cur_number != connexe[x][y - 1]) {
 *                     int target_number = connexe[x][y - 1];
 *                     for (x2 = 0; x2 < connexe.length; x2++) {
 *                         for (y2 = 0; y2 <= y; y2++) {
 *                             if (connexe[x2][y2] == target_number)
 *                                 connexe[x2][y2] = cur_number;
 *                         }
 *                     }
 *                 }
 *             }
 *
 *             // On regarde si y'a un numéro de composante
 *             if (cur_number == 0) {
 *                 // Si y'en a pas on lui en donen un
 *                 connexe[x][y] = ni;
 *                 ni++;
 *             } else {
 *                 // Si y'en a un on le met
 *                 connexe[x][y] = cur_number;
 *             }
 *         }
 *     }
 *     for (var cell : this.cells) {
 *         cell.composante = connexe[cell.getX() - this.min_x][cell.getY() - this.min_y];
 *     }
 * }
 */
void lw_map_compute_composantes(LwMap *self) {
    int sx = self->max_x - self->min_x + 1;
    int sy = self->max_y - self->min_y + 1;

    /* Stack-allocated 2D scratch -- bounded by COORD_DIM. */
    static int connexe[LW_MAP_COORD_DIM][LW_MAP_COORD_DIM];

    int x, y, x2, y2, ni = 1;
    for (x = 0; x < sx; x++) {
        for (y = 0; y < sy; y++)
            connexe[x][y] = -1;
    }

    /* On cherche les composantes connexes */
    for (x = 0; x < sx; x++) {
        for (y = 0; y < sy; y++) {
            int c_id = self->coord[x][y];
            LwCell *c = (c_id >= 0) ? &self->cells[c_id] : NULL;
            if (c == NULL) {
                continue;
            }
            int cur_number = 0;
            int west_id  = (x > 0) ? self->coord[x - 1][y] : -1;
            LwCell *west_cell = (west_id >= 0) ? &self->cells[west_id] : NULL;
            if (x > 0 && west_cell != NULL && lw_cell_is_walkable(west_cell) == lw_cell_is_walkable(c))
                cur_number = connexe[x - 1][y];

            int north_id = (y > 0) ? self->coord[x][y - 1] : -1;
            LwCell *north_cell = (north_id >= 0) ? &self->cells[north_id] : NULL;
            if (y > 0 && north_cell != NULL && lw_cell_is_walkable(north_cell) == lw_cell_is_walkable(c)) {
                if (cur_number == 0)
                    cur_number = connexe[x][y - 1];
                else if (cur_number != connexe[x][y - 1]) {
                    int target_number = connexe[x][y - 1];
                    for (x2 = 0; x2 < sx; x2++) {
                        for (y2 = 0; y2 <= y; y2++) {
                            if (connexe[x2][y2] == target_number)
                                connexe[x2][y2] = cur_number;
                        }
                    }
                }
            }

            /* On regarde si y'a un numéro de composante */
            if (cur_number == 0) {
                /* Si y'en a pas on lui en donen un */
                connexe[x][y] = ni;
                ni++;
            } else {
                /* Si y'en a un on le met */
                connexe[x][y] = cur_number;
            }
        }
    }
    for (int i = 0; i < self->nb_cells; i++) {
        LwCell *cell = &self->cells[i];
        cell->composante = connexe[cell->x - self->min_x][cell->y - self->min_y];
    }
    self->composantes = ni - 1;
}


/* public Cell getPushLastAvailableCell(Cell entity, Cell target, Cell caster) {
 *     // Delta caster --> entity
 *     int cdx = (int) Math.signum(entity.x - caster.x);
 *     int cdy = (int) Math.signum(entity.y - caster.y);
 *     // Delta entity --> target
 *     int dx = (int) Math.signum(target.x - entity.x);
 *     int dy = (int) Math.signum(target.y - entity.y);
 *     // Check deltas (must be pushed in the correct direction)
 *     if (cdx != dx || cdy != dy) return entity; // no change
 *     Cell current = entity;
 *     while (current != target) {
 *         Cell next = current.next(this, dx, dy);
 *         if (!next.available(this)) {
 *             return current;
 *         }
 *         current = next;
 *     }
 *     return current;
 * }
 */
LwCell* lw_map_get_push_last_available_cell(LwMap *self,
                                            LwCell *entity,
                                            LwCell *target,
                                            const LwCell *caster) {
    /* signum semantics: -1, 0, or +1 (matches Java's Math.signum then
     * cast to int -- which truncates toward zero). */
    int cdx = (entity->x - caster->x > 0) ? 1 : ((entity->x - caster->x < 0) ? -1 : 0);
    int cdy = (entity->y - caster->y > 0) ? 1 : ((entity->y - caster->y < 0) ? -1 : 0);
    int dx  = (target->x - entity->x > 0) ? 1 : ((target->x - entity->x < 0) ? -1 : 0);
    int dy  = (target->y - entity->y > 0) ? 1 : ((target->y - entity->y < 0) ? -1 : 0);
    /* Check deltas (must be pushed in the correct direction) */
    if (cdx != dx || cdy != dy) return entity; /* no change */
    LwCell *current = entity;
    while (current != target) {
        LwCell *next = lw_cell_next(current, self, dx, dy);
        if (next == NULL || !lw_cell_available(next, self)) {
            return current;
        }
        current = next;
    }
    return current;
}


/* public Cell getAttractLastAvailableCell(Cell entity, Cell target, Cell caster) {
 *     // Delta caster --> entity
 *     int cdx = (int) Math.signum(entity.x - caster.x);
 *     int cdy = (int) Math.signum(entity.y - caster.y);
 *     // Delta entity --> target
 *     int dx = (int) Math.signum(target.x - entity.x);
 *     int dy = (int) Math.signum(target.y - entity.y);
 *     // Check deltas (must be attracted in the correct direction)
 *     if (cdx != -dx || cdy != -dy) return entity; // no change
 *     Cell current = entity;
 *     while (current != target) {
 *         Cell next = current.next(this, dx, dy);
 *         if (!next.available(this)) {
 *             return current;
 *         }
 *         current = next;
 *     }
 *     return current;
 * }
 */
LwCell* lw_map_get_attract_last_available_cell(LwMap *self,
                                               LwCell *entity,
                                               LwCell *target,
                                               const LwCell *caster) {
    /* signum semantics: 0/+1/-1 */
    int cdx = (entity->x - caster->x > 0) ? 1 : ((entity->x - caster->x < 0) ? -1 : 0);
    int cdy = (entity->y - caster->y > 0) ? 1 : ((entity->y - caster->y < 0) ? -1 : 0);
    int dx  = (target->x - entity->x > 0) ? 1 : ((target->x - entity->x < 0) ? -1 : 0);
    int dy  = (target->y - entity->y > 0) ? 1 : ((target->y - entity->y < 0) ? -1 : 0);
    /* Check deltas (must be attracted in the correct direction) */
    if (cdx != -dx || cdy != -dy) return entity; /* no change */
    LwCell *current = entity;
    while (current != target) {
        LwCell *next = lw_cell_next(current, self, dx, dy);
        if (next == NULL || !lw_cell_available(next, self)) {
            return current;
        }
        current = next;
    }
    return current;
}


/* public boolean canUseAttack(Cell caster, Cell target, Attack attack) {
 *     // Portée
 *     if (!verifyRange(caster, target, attack)) {
 *         return false;
 *     }
 *     return verifyLoS(caster, target, attack);
 * }
 */
int lw_map_can_use_attack(LwMap *self,
                          const LwCell *caster,
                          const LwCell *target,
                          const struct LwAttack *attack) {
    /* Portée */
    if (!lw_map_verify_range(self, caster, target, attack)) {
        return 0;
    }
    return lw_map_verify_los(self, caster, target, attack);
}


/* public boolean verifyRange(Cell caster, Cell target, Attack attack) {
 *     if (target == null || caster == null) {
 *         return false;
 *     }
 *     int dx = caster.getX() - target.getX();
 *     int dy = caster.getY() - target.getY();
 *     int distance = Math.abs(dx) + Math.abs(dy);
 *
 *     // Pour tous les types : vérification de la distance
 *     if (distance > attack.getMaxRange() || distance < attack.getMinRange()) {
 *         return false;
 *     }
 *     // Même cellule, OK
 *     if (caster == target) return true;
 *
 *     // Vérification de chaque type de lancé
 *     if ((attack.getLaunchType() & 1) == 0 && (dx == 0 || dy == 0)) return false; // Ligne
 *     if ((attack.getLaunchType() & 2) == 0 && Math.abs(dx) == Math.abs(dy)) return false; // Diagonale
 *     if ((attack.getLaunchType() & 4) == 0 && Math.abs(dx) != Math.abs(dy) && dx != 0 && dy != 0) return false; // Reste
 *
 *     return true;
 * }
 */
int lw_map_verify_range(LwMap *self,
                        const LwCell *caster,
                        const LwCell *target,
                        const struct LwAttack *attack) {
    (void)self;
    if (target == NULL || caster == NULL) {
        return 0;
    }
    int dx = caster->x - target->x;
    int dy = caster->y - target->y;
    int distance = abs(dx) + abs(dy);

    /* Pour tous les types : vérification de la distance */
    if (distance > lw_attack_get_max_range(attack) || distance < lw_attack_get_min_range(attack)) {
        return 0;
    }
    /* Même cellule, OK */
    if (caster == target) return 1;

    int launch_type = lw_attack_get_launch_type(attack);
    /* Vérification de chaque type de lancé */
    if ((launch_type & 1) == 0 && (dx == 0 || dy == 0)) return 0; /* Ligne */
    if ((launch_type & 2) == 0 && abs(dx) == abs(dy)) return 0; /* Diagonale */
    if ((launch_type & 4) == 0 && abs(dx) != abs(dy) && dx != 0 && dy != 0) return 0; /* Reste */

    return 1;
}


/* ============================================================== */
/* generateMap (public static)                                     */
/* ============================================================== */

/* public static Map generateMap(State state, int context, int width,
 *                               int height, int obstacles_count,
 *                               List<Team> teams, ObjectNode custom_map)
 *
 * The control flow:
 *  - if custom_map != null: place custom obstacles, then place team
 *    cells (using getRandomCell(state, part) when team1/team2 hasn't
 *    overridden the cell). Calls computeComposantes() once.
 *  - else: loop up to 63 times, generating obstacles + placing entities
 *    on each iteration; first iteration that produces a valid map
 *    (all entities reachable from each other) wins.
 *  - either branch: setType from getInt(0,4), then maybe overwrite for
 *    test/tournament/custom-typed contexts.
 */
/* Renamed: state.c expects an LwMap* return + opaque custom_map ptr.
 * The wrapper in lw_glue.c provides that signature; this is the
 * port's full impl with split custom-map params. */
void lw_map_generate_map_impl(LwMap *out_map,
                         struct LwState *state,
                         int context,
                         int width, int height,
                         int obstacles_count,
                         struct LwTeam **teams, int n_teams,
                         int custom_map_id,
                         int custom_pattern,
                         int custom_type,
                         const LwCustomObstacle *custom_obstacles,
                         int n_custom_obstacles,
                         const int *team1_cells, int n_team1_cells,
                         const int *team2_cells, int n_team2_cells) {

    /* Java: boolean valid = false; int nb = 0; Map map = null;
     * Our `out_map` *is* the Map -- it's always non-null, init'd inside
     * one of the two branches below. */
    int valid = 0;
    int nb = 0;

    if (custom_obstacles != NULL) {

        /* int mapId = custom_map.hasNonNull("id") ? custom_map.get("id").intValue() : 0;
         * map = new Map(width, height, mapId);
         * map.custom_map = custom_map;
         * map.pattern = (ArrayNode) custom_map.get("pattern");
         * map.state = state;
         */
        lw_map_init_with_id(out_map, width, height, custom_map_id);
        out_map->pattern = custom_pattern;
        out_map->state = state;

        /* Obstacles */
        for (int oi = 0; oi < n_custom_obstacles; oi++) {
            int cell_id = custom_obstacles[oi].cell_id;
            int kind = custom_obstacles[oi].kind;
            LwCell *cell = lw_map_get_cell(out_map, cell_id);
            if (cell == NULL) continue;
            if (lw_cell_available(cell, out_map)) {
                if (kind == 0) {
                    /* Java: c.getValue().isBoolean() branch */
                    lw_cell_set_obstacle(cell, 1, 1);
                } else {
                    int id = kind;
                    const LwObstacleInfo *info = lw_obstacle_info_get(id);
                    if (info == NULL) continue;
                    if (info->size == 1) {
                        lw_cell_set_obstacle(cell, id, info->size);
                    } else if (info->size == 2) {
                        lw_cell_set_obstacle(cell, id, info->size);
                        place_size2_tail(out_map, cell);
                    } else if (info->size == 3) {
                        lw_cell_set_obstacle(cell, id, info->size);
                        for (int x = -1; x <= 1; ++x) {
                            for (int y = -1; y <= 1; ++y) {
                                if (x != 0 || y != 0) {
                                    LwCell *nx = lw_map_get_next_cell(out_map, cell, x, y);
                                    if (nx != NULL) lw_cell_set_obstacle(nx, 0, -1);
                                }
                            }
                        }
                    } else if (info->size == 4) {
                        lw_cell_set_obstacle(cell, id, info->size);
                        LwCell *nx = lw_map_get_next_cell(out_map, cell, -3, 0);
                        if (nx != NULL) lw_cell_set_obstacle(nx, 0, -1);
                    } else if (info->size == 5) {
                        lw_cell_set_obstacle(cell, id, info->size);
                        /* [[0, -1], [0, 0], [0, 3], [2, -1], [2, 0], [2, 3]] */
                        LwCell *nx;
                        nx = lw_map_get_next_cell(out_map, cell, 0, -1); if (nx) lw_cell_set_obstacle(nx, 0, -1);
                        nx = lw_map_get_next_cell(out_map, cell, 0,  3); if (nx) lw_cell_set_obstacle(nx, 0, -1);
                        nx = lw_map_get_next_cell(out_map, cell, 2, -1); if (nx) lw_cell_set_obstacle(nx, 0, -1);
                        nx = lw_map_get_next_cell(out_map, cell, 2,  0); if (nx) lw_cell_set_obstacle(nx, 0, -1);
                        nx = lw_map_get_next_cell(out_map, cell, 2,  3); if (nx) lw_cell_set_obstacle(nx, 0, -1);
                    }
                }
            }
        }

        /* Set entities positions */
        for (int t = 0; t < n_teams; ++t) {
            int pos = 0;
            int n_team_entities = lw_team_get_entity_count(teams[t]);
            for (int ei = 0; ei < n_team_entities; ei++) {
                int entity_idx = lw_team_get_entity_idx(teams[t], ei);
                struct LwEntity *l = lw_state_get_entity(state, entity_idx);
                if (l == NULL || lw_entity_is_dead(l)) continue;
                /* Random cell */
                LwCell *c;
                int initial_cell_id = lw_entity_get_initial_cell(l);
                if (out_map->id != 0 && initial_cell_id >= 0) {
                    c = lw_map_get_cell(out_map, initial_cell_id);
                } else {
                    if (n_teams == 2) { /* 2 teams : 2 sides */
                        c = lw_map_get_random_cell_part(out_map, state, t == 0 ? 1 : 4);
                    } else { /* 2+ teams : random */
                        c = lw_map_get_random_cell(out_map, state);
                    }
                    /* User custom cell? */
                    if (t < 2) {
                        const int *team_cells = (t == 0) ? team1_cells : team2_cells;
                        int n_team_cells = (t == 0) ? n_team1_cells : n_team2_cells;
                        if (team_cells != NULL) {
                            if (pos < n_team_cells) {
                                int cell_id = team_cells[pos++];
                                if (cell_id >= 0 || cell_id < out_map->nb_cells) {
                                    c = lw_map_get_cell(out_map, cell_id);
                                }
                            }
                        }
                    }
                }
                if (c != NULL) {
                    /* entity_idx is the entity's FID; resolve to LwEntity*
                     * for the new lw_map_set_entity signature. */
                    struct LwEntity *resolved = lw_state_get_entity(state, entity_idx);
                    if (resolved != NULL) lw_map_set_entity(out_map, resolved, c);
                }
            }
        }

        lw_map_compute_composantes(out_map);

    } else {

        /* Iteration buffer for "leeks" (entity-array-indices). */
        int leeks[LW_MAP_MAX_CELLS];
        int n_leeks = 0;

        while (!valid && nb++ < 63) {

            lw_map_init(out_map, width, height);
            out_map->state = state;

            for (int i = 0; i < obstacles_count; i++) {
                LwCell *c = lw_map_get_cell(out_map, lw_rng_get_int(&state->rng_n, 0, lw_map_get_nb_cell(out_map)));
                if (c != NULL && lw_cell_available(c, out_map)) {
                    int size = lw_rng_get_int(&state->rng_n, 1, 2);
                    int type = lw_rng_get_int(&state->rng_n, 0, 2);
                    if (size == 2) {
                        LwCell *c2 = lw_map_get_cell_by_dir(out_map, c,  LW_DIR_EAST);
                        LwCell *c3 = lw_map_get_cell_by_dir(out_map, c,  LW_DIR_SOUTH);
                        LwCell *c4 = (c3 != NULL) ? lw_map_get_cell_by_dir(out_map, c3, LW_DIR_EAST) : NULL;
                        if (c2 == NULL || c3 == NULL || c4 == NULL ||
                            !lw_cell_available(c2, out_map) ||
                            !lw_cell_available(c3, out_map) ||
                            !lw_cell_available(c4, out_map))
                            size = 1;
                        else {
                            lw_cell_set_obstacle(c2, 0, -1);
                            lw_cell_set_obstacle(c3, 0, -2);
                            lw_cell_set_obstacle(c4, 0, -3);
                        }
                    }
                    lw_cell_set_obstacle(c, type, size);
                }
            }
            lw_map_compute_composantes(out_map);
            n_leeks = 0;

            /* Set entities positions */
            for (int t = 0; t < n_teams; ++t) {

                int n_team_entities = lw_team_get_entity_count(teams[t]);
                for (int ei = 0; ei < n_team_entities; ei++) {
                    int entity_idx = lw_team_get_entity_idx(teams[t], ei);
                    struct LwEntity *l = lw_state_get_entity(state, entity_idx);
                    if (l == NULL) continue;

                    LwCell *c;
                    int state_type = lw_state_get_type(state);
                    if (state_type == LW_FIGHT_TYPE_BATTLE_ROYALE) { /* BR : random */

                        c = lw_map_get_random_cell(out_map, state);

                    } else { /* 2 sides */

                        if (state_type == LW_FIGHT_TYPE_CHEST_HUNT) {
                            /* Chest hunt: chests at center (distance <= 5), players around (distance >= 10) */
                            if (lw_entity_get_type(l) == LW_ENTITY_TYPE_CHEST) {
                                c = lw_map_get_random_cell_near_center(out_map, state, 3);
                            } else {
                                c = lw_map_get_random_cell_away_from_center(out_map, state, 12);
                            }
                        } else if (lw_entity_get_type(l) == LW_ENTITY_TYPE_CHEST) {
                            /* Classic chests: equal distance between teams */
                            int team0HasCells = 0;
                            int team0_n = lw_state_get_team_entity_count(state, 0);
                            for (int k = 0; k < team0_n; k++) {
                                int eidx = lw_state_get_team_entity(state, 0, k);
                                struct LwEntity *e = lw_state_get_entity(state, eidx);
                                if (e != NULL && lw_entity_get_cell_id(e) >= 0) { team0HasCells = 1; break; }
                            }
                            int team1HasCells = 0;
                            if (n_teams > 1) {
                                int team1_n = lw_state_get_team_entity_count(state, 1);
                                for (int k = 0; k < team1_n; k++) {
                                    int eidx = lw_state_get_team_entity(state, 1, k);
                                    struct LwEntity *e = lw_state_get_entity(state, eidx);
                                    if (e != NULL && lw_entity_get_cell_id(e) >= 0) { team1HasCells = 1; break; }
                                }
                            }
                            c = (team0HasCells && team1HasCells)
                                ? lw_map_get_cell_equal_distance(out_map, state)
                                : lw_map_get_random_cell(out_map, state);
                        } else {
                            c = lw_map_get_random_cell_part(out_map, state, t == 0 ? 1 : 4);
                        }
                    }
                    if (c == NULL) continue;

                    lw_map_set_entity(out_map, entity_idx, c);
                    if (n_leeks < LW_MAP_MAX_CELLS) leeks[n_leeks++] = entity_idx;

                    /* If turret, remove obstacles 5 cells around */
                    if (lw_entity_get_type(l) == LW_ENTITY_TYPE_TURRET) {
                        LwCell *circle_cells[LW_MAP_MAX_CELLS];
                        int n_circle = get_cells_in_circle(out_map, c, 5,
                                                            circle_cells, LW_MAP_MAX_CELLS);
                        for (int k = 0; k < n_circle; k++) {
                            remove_obstacle(out_map, circle_cells[k]);
                        }
                    }
                }
            }

            /* Check paths */
            valid = 1;
            if (n_leeks > 0) {
                struct LwEntity *e0 = lw_state_get_entity(state, leeks[0]);
                int cell0_id = (e0 != NULL) ? lw_entity_get_cell_id(e0) : -1;
                if (cell0_id < 0) { valid = 0; }
                else {
                    int composante = self_composante(out_map, cell0_id);
                    for (int i = 1; i < n_leeks; i++) {
                        struct LwEntity *ei = lw_state_get_entity(state, leeks[i]);
                        int celli_id = (ei != NULL) ? lw_entity_get_cell_id(ei) : -1;
                        if (celli_id < 0 || composante != self_composante(out_map, celli_id)) {
                            valid = 0;
                            break;
                        }
                    }
                }
            }
        }
    }

    /* Generate type */
    lw_map_set_type(out_map, lw_rng_get_int(&state->rng_n, 0, 4));

    if (context == LW_CONTEXT_TEST) {
        lw_map_set_type(out_map, -1); /* Nexus */
    } else if (context == LW_CONTEXT_TOURNAMENT) {
        lw_map_set_type(out_map, 5); /* Arena */
    } else if (custom_obstacles != NULL && custom_type >= 0) {
        lw_map_set_type(out_map, custom_type);
    }
}


/* Helper to fetch a cell's connected-component number by cell id.
 * Used inside generateMap; pulled out so we don't pollute the public API. */
static int self_composante(const LwMap *self, int cell_id) {
    if (cell_id < 0 || cell_id >= self->nb_cells) return -1;
    return self->cells[cell_id].composante;
}


/* ---- lw_cell.h forward-declared methods ------------------------- */

/* public boolean Cell.available(Map map) {
 *     return walkable && map.getEntity(this) == null;
 * }
 */
int lw_cell_available(const LwCell *self, const struct LwMap *map) {
    if (!self->walkable) return 0;
    return lw_map_get_entity(map, self) == -1 ? 1 : 0;
}


/* public Entity Cell.getPlayer(Map map) { return map.getEntity(this); }
 *
 * Returns the LwEntity record, NULL if the cell is empty. */
struct LwEntity* lw_cell_get_player(const LwCell *self, const struct LwMap *map) {
    int idx = lw_map_get_entity(map, self);
    if (idx < 0) return NULL;
    /* `state` may be NULL on a freshly initialised map (no fight bound
     * yet). Match Java's null-safety: just return NULL. */
    if (map->state == NULL) return NULL;
    return lw_state_get_entity((struct LwState *)map->state, idx);
}


/* public Cell Cell.next(Map map, int dx, int dy) {
 *     return map.getCell(this.x + dx, this.y + dy);
 * }
 */
LwCell* lw_cell_next(const LwCell *self, const struct LwMap *map, int dx, int dy) {
    /* The const-cast mirrors Java's lack of const semantics; map is not
     * mutated by getCell(x,y). */
    return lw_map_get_cell_xy((LwMap *)map, self->x + dx, self->y + dy);
}
