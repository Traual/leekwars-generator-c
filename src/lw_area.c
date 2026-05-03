/*
 * lw_area.c -- 1:1 port of area/Area.java + all 16 subclasses + the
 * MaskAreaCell mask generators they depend on.
 *
 * Each Java AreaXxx.getArea() becomes a static lw_area_xxx_get_area()
 * function below. Dispatch happens in lw_area_get_area().
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/area/Area.java
 *   java_reference/src/main/java/com/leekwars/generator/area/Area*.java
 *   java_reference/src/main/java/com/leekwars/generator/area/MaskArea.java
 *   java_reference/src/main/java/com/leekwars/generator/maps/MaskAreaCell.java
 */
#include "lw_area.h"
#include "lw_attack.h"  /* min_range / max_range / need_los static inlines */
#include "lw_entity.h"
#include "lw_map.h"

#include <stddef.h>


/* ---- Forward decls for the engine APIs we call --------------------
 *
 * Map / Attack / Leek live in their own (mostly not-yet-ported)
 * headers. We only need the function signatures here; the linker
 * resolves them once those modules exist. */

/* maps/Map.java */
LwCell *lw_map_get_cell_xy(const struct LwMap *map, int x, int y);
LwCell *lw_map_get_first_entity(const struct LwMap *map,
                                LwCell *from, LwCell *target,
                                int min_range, int max_range);
struct LwState *lw_map_get_state(const struct LwMap *map);

/* state/State.java -- iterating map.getState().getEntities().values()
 * Returns a contiguous array (state owns the storage) and writes the
 * count to *out_n. Iteration order matches Java's HashMap insertion
 * iteration -- the State port is responsible for preserving it. */
struct LwLeek **lw_state_get_entities(const struct LwState *state, int *out_n);

/* attack/Attack.java */
int lw_attack_get_min_range(const struct LwAttack *self);
int lw_attack_get_max_range(const struct LwAttack *self);
int lw_attack_need_los    (const struct LwAttack *self);

/* state/Entity.java (we use LwLeek as the caster type per spec). */
int          lw_leek_get_team(const struct LwLeek *self);
const char  *lw_leek_get_name(const struct LwLeek *self);
LwCell      *lw_leek_get_cell(const struct LwLeek *self);


/* ---- Tiny string helper (Java: String.contains) ------------------ */

static int lw_str_contains(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL) return 0;
    for (const char *h = haystack; *h; ++h) {
        const char *a = h, *b = needle;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*b == '\0') return 1;
    }
    return 0;
}

static int lw_abs_i(int v) { return v < 0 ? -v : v; }


/* ---- MaskAreaCell.java -------------------------------------------
 *
 * Java declares each AreaXxx subclass with
 *
 *     private static int[][] area = MaskAreaCell.generateXxxMask(...);
 *
 * which builds the mask once at class-load time. In C we precompute
 * the masks lazily into module-static buffers on first use.
 *
 * Maximum cell counts (verified by running the formulas):
 *   CIRCLE1 :  5    PLUS_2  :  9
 *   CIRCLE2 : 13    PLUS_3  : 13
 *   CIRCLE3 : 25    X_1     :  5
 *   SQUARE_1:  9    X_2     :  9
 *   SQUARE_2: 25    X_3     : 13
 *
 * Largest is 25 -- we size every buffer to 32 to keep the math
 * obvious.
 */

#define LW_AREA_MASK_CAP 32

typedef struct {
    int built;
    int n;
    int cells[LW_AREA_MASK_CAP][2];
} LwAreaMask;


/* public static int[][] generateCircleMask(int min, int max) */
static void lw_mask_generate_circle(LwAreaMask *m, int min, int max) {
    if (min > max) {
        m->n = 0;
        return;
    }
    int index = 0;
    if (min == 0) {
        // Center first
        m->cells[index][0] = 0; m->cells[index][1] = 0; index++;
    }

    // Go from cells closer to the center to the farther ones
    for (int size = (min < 1 ? 1 : min); size <= max; size++) {
        // Add cells counter-clockwise
        for (int i = 0; i < size; i++) {
            m->cells[index][0] = size - i; m->cells[index][1] = -i; index++;
        }
        for (int i = 0; i < size; i++) {
            m->cells[index][0] = -i; m->cells[index][1] = -(size - i); index++;
        }
        for (int i = 0; i < size; i++) {
            m->cells[index][0] = -(size - i); m->cells[index][1] = i; index++;
        }
        for (int i = 0; i < size; i++) {
            m->cells[index][0] = i; m->cells[index][1] = size - i; index++;
        }
    }
    m->n = index;
}

/* public static int[][] generatePlusMask(int radius) */
static void lw_mask_generate_plus(LwAreaMask *m, int radius) {
    int index = 0;
    // Center first
    m->cells[index][0] = 0; m->cells[index][1] = 0; index++;

    // Go from cells closer to the center to the farther ones
    for (int size = 1; size <= radius; size++) {
        // Add cells counter-clockwise
        m->cells[index][0] =  size; m->cells[index][1] =     0; index++;
        m->cells[index][0] =     0; m->cells[index][1] = -size; index++;
        m->cells[index][0] = -size; m->cells[index][1] =     0; index++;
        m->cells[index][0] =     0; m->cells[index][1] =  size; index++;
    }
    m->n = index;
}

/* public static int[][] generateXMask(int radius) */
static void lw_mask_generate_x(LwAreaMask *m, int radius) {
    int index = 0;
    // Center first
    m->cells[index][0] = 0; m->cells[index][1] = 0; index++;

    // Go from cells closer to the center to the farther ones
    for (int size = 1; size <= radius; size++) {
        // Add cells counter-clockwise
        m->cells[index][0] =  size; m->cells[index][1] = -size; index++;
        m->cells[index][0] = -size; m->cells[index][1] = -size; index++;
        m->cells[index][0] = -size; m->cells[index][1] =  size; index++;
        m->cells[index][0] =  size; m->cells[index][1] =  size; index++;
    }
    m->n = index;
}

/* public static int[][] generateSquareMask(int radius) */
static void lw_mask_generate_square(LwAreaMask *m, int radius) {
    // Go from cells closer to the center to the farther ones
    // First, add cells in the inscribed circle
    LwAreaMask circle;
    lw_mask_generate_circle(&circle, 0, radius);

    int index = 0;
    for (int i = 0; i < circle.n; i++) {
        m->cells[index][0] = circle.cells[i][0];
        m->cells[index][1] = circle.cells[i][1];
        index++;
    }
    // Then, the corners
    for (int d = 0; d < radius; d++) {
        // Add cells counter-clockwise
        for (int i = 1; i <= radius - d; i++) {
            m->cells[index][0] =  radius + 1 - i; m->cells[index][1] = -(d + i); index++;
        }
        for (int i = 1; i <= radius - d; i++) {
            m->cells[index][0] = -(d + i); m->cells[index][1] = -(radius + 1 - i); index++;
        }
        for (int i = 1; i <= radius - d; i++) {
            m->cells[index][0] = -(radius + 1 - i); m->cells[index][1] = d + i; index++;
        }
        for (int i = 1; i <= radius - d; i++) {
            m->cells[index][0] = d + i; m->cells[index][1] = radius + 1 - i; index++;
        }
    }
    m->n = index;
}


/* Lazy accessors for the per-subclass masks. Each Java
 * `private static int[][] area = MaskAreaCell.generate...()` becomes
 * a one-shot init below. */

static LwAreaMask g_mask_circle1, g_mask_circle2, g_mask_circle3;
static LwAreaMask g_mask_plus2,   g_mask_plus3;
static LwAreaMask g_mask_x1,      g_mask_x2,      g_mask_x3;
static LwAreaMask g_mask_square1, g_mask_square2;

static const LwAreaMask *lw_mask_for_tag(LwAreaType tag) {
    switch (tag) {
        case LW_AREA_TAG_CIRCLE1:
            if (!g_mask_circle1.built) { lw_mask_generate_circle(&g_mask_circle1, 0, 1); g_mask_circle1.built = 1; }
            return &g_mask_circle1;
        case LW_AREA_TAG_CIRCLE2:
            if (!g_mask_circle2.built) { lw_mask_generate_circle(&g_mask_circle2, 0, 2); g_mask_circle2.built = 1; }
            return &g_mask_circle2;
        case LW_AREA_TAG_CIRCLE3:
            if (!g_mask_circle3.built) { lw_mask_generate_circle(&g_mask_circle3, 0, 3); g_mask_circle3.built = 1; }
            return &g_mask_circle3;
        case LW_AREA_TAG_PLUS_2:
            if (!g_mask_plus2.built) { lw_mask_generate_plus(&g_mask_plus2, 2); g_mask_plus2.built = 1; }
            return &g_mask_plus2;
        case LW_AREA_TAG_PLUS_3:
            if (!g_mask_plus3.built) { lw_mask_generate_plus(&g_mask_plus3, 3); g_mask_plus3.built = 1; }
            return &g_mask_plus3;
        case LW_AREA_TAG_X_1:
            if (!g_mask_x1.built) { lw_mask_generate_x(&g_mask_x1, 1); g_mask_x1.built = 1; }
            return &g_mask_x1;
        case LW_AREA_TAG_X_2:
            if (!g_mask_x2.built) { lw_mask_generate_x(&g_mask_x2, 2); g_mask_x2.built = 1; }
            return &g_mask_x2;
        case LW_AREA_TAG_X_3:
            if (!g_mask_x3.built) { lw_mask_generate_x(&g_mask_x3, 3); g_mask_x3.built = 1; }
            return &g_mask_x3;
        case LW_AREA_TAG_SQUARE_1:
            if (!g_mask_square1.built) { lw_mask_generate_square(&g_mask_square1, 1); g_mask_square1.built = 1; }
            return &g_mask_square1;
        case LW_AREA_TAG_SQUARE_2:
            if (!g_mask_square2.built) { lw_mask_generate_square(&g_mask_square2, 2); g_mask_square2.built = 1; }
            return &g_mask_square2;
        default:
            return NULL;
    }
}


/* ---- AreaSingleCell.java -----------------------------------------
 *
 * @Override
 * public List<Cell> getArea(Map map, Cell launchCell, Cell targetCell, Entity caster) {
 *     ArrayList<Cell> area = new ArrayList<Cell>();
 *     area.add(targetCell);
 *     return area;
 * }
 */
static int lw_area_single_cell_get_area(const struct LwMap *map,
                                        LwCell *launchCell, LwCell *targetCell,
                                        struct LwLeek *caster,
                                        LwCell *out_buf, int out_cap) {
    (void)map; (void)launchCell; (void)caster;
    int n = 0;
    if (targetCell != NULL) {
        if (n < out_cap) out_buf[n] = *targetCell;
        n++;
    }
    return n;
}


/* ---- MaskArea.java (used by Circle/Plus/X/Square subclasses) -----
 *
 * @Override
 * public List<Cell> getArea(Map map, Cell launchCell, Cell targetCell, Entity caster) {
 *     int x = targetCell.getX(), y = targetCell.getY();
 *     ArrayList<Cell> cells = new ArrayList<Cell>();
 *     for (int i = 0; i < area.length; i++) {
 *         Cell c = map.getCell(x + area[i][0], y + area[i][1]);
 *         if (c == null || !c.isWalkable())
 *             continue;
 *         cells.add(c);
 *     }
 *     return cells;
 * }
 */
static int lw_area_mask_get_area(const LwAreaMask *area,
                                 const struct LwMap *map,
                                 LwCell *launchCell, LwCell *targetCell,
                                 struct LwLeek *caster,
                                 LwCell *out_buf, int out_cap) {
    (void)launchCell; (void)caster;
    int n = 0;
    if (targetCell == NULL || area == NULL) return 0;
    int x = targetCell->x, y = targetCell->y;
    for (int i = 0; i < area->n; i++) {
        LwCell *c = lw_map_get_cell_xy(map, x + area->cells[i][0], y + area->cells[i][1]);
        if (c == NULL || !c->walkable)
            continue;
        if (n < out_cap) out_buf[n] = *c;
        n++;
    }
    return n;
}


/* ---- AreaLaserLine.java ------------------------------------------
 *
 * @Override
 * public List<Cell> getArea(Map map, Cell launchCell, Cell targetCell, Entity caster) {
 *     ArrayList<Cell> cells = new ArrayList<Cell>();
 *     int dx = 0, dy = 0;
 *     if (launchCell.getX() == targetCell.getX()) {
 *         if (launchCell.getY() > targetCell.getY()) dy = -1; else dy = 1;
 *     } else if (launchCell.getY() == targetCell.getY()) {
 *         if (launchCell.getX() > targetCell.getX()) dx = -1; else dx = 1;
 *     } else
 *         return cells;
 *
 *     int x = launchCell.getX(), y = launchCell.getY();
 *     for (int i = mAttack.getMinRange(); i <= mAttack.getMaxRange(); i++) {
 *         Cell c = map.getCell(x + dx * i, y + dy * i);
 *         if (c == null) break;
 *         if (mAttack.needLos() && !c.isWalkable()) break;
 *         else if (mAttack.needLos() && !c.isWalkable()) break;
 *         cells.add(c);
 *     }
 *     return cells;
 * }
 *
 * NOTE: The duplicated `else if` clause above mirrors the Java source
 * verbatim (it's a no-op duplicate -- left as-is for parity).
 */
static int lw_area_laser_line_get_area(const struct LwAttack *mAttack,
                                       const struct LwMap *map,
                                       LwCell *launchCell, LwCell *targetCell,
                                       struct LwLeek *caster,
                                       LwCell *out_buf, int out_cap) {
    (void)caster;
    int n = 0;
    int dx = 0, dy = 0;
    if (launchCell == NULL || targetCell == NULL) return 0;
    if (launchCell->x == targetCell->x) {
        if (launchCell->y > targetCell->y)
            dy = -1;
        else
            dy = 1;
    } else if (launchCell->y == targetCell->y) {
        if (launchCell->x > targetCell->x)
            dx = -1;
        else
            dx = 1;
    } else
        return n;

    int x = launchCell->x, y = launchCell->y;
    for (int i = lw_attack_get_min_range(mAttack); i <= lw_attack_get_max_range(mAttack); i++) {

        LwCell *c = lw_map_get_cell_xy(map, x + dx * i, y + dy * i);
        if (c == NULL) {
            break;
        }
        if (lw_attack_need_los(mAttack) && !c->walkable) {
            break;
        } else if (lw_attack_need_los(mAttack) && !c->walkable) {
            break;
        }
        if (n < out_cap) out_buf[n] = *c;
        n++;
    }
    return n;
}


/* ---- AreaFirstInLine.java ----------------------------------------
 *
 * @Override
 * public List<Cell> getArea(Map map, Cell launchCell, Cell targetCell, Entity caster) {
 *     List<Cell> cells = new ArrayList<>();
 *     Cell cell = map.getFirstEntity(launchCell, targetCell, mAttack.getMinRange(), mAttack.getMaxRange());
 *     if (cell != null) {
 *         cells.add(cell);
 *     }
 *     return cells;
 * }
 */
static int lw_area_first_in_line_get_area(const struct LwAttack *mAttack,
                                          const struct LwMap *map,
                                          LwCell *launchCell, LwCell *targetCell,
                                          struct LwLeek *caster,
                                          LwCell *out_buf, int out_cap) {
    (void)caster;
    int n = 0;
    LwCell *cell = lw_map_get_first_entity(map, launchCell, targetCell,
                                           lw_attack_get_min_range(mAttack),
                                           lw_attack_get_max_range(mAttack));
    if (cell != NULL) {
        if (n < out_cap) out_buf[n] = *cell;
        n++;
    }
    return n;
}


/* ---- AreaAllies.java ---------------------------------------------
 *
 * @Override
 * public List<Cell> getArea(Map map, Cell launchCell, Cell targetCell, Entity caster) {
 *     var cells = new ArrayList<Cell>();
 *     if (caster != null) {
 *         for (var entity : map.getState().getEntities().values()) {
 *             if (entity.getTeam() == caster.getTeam() && entity.getName().contains("crystal")) continue;
 *             if (entity.getCell() != null && entity.getTeam() == caster.getTeam()) {
 *                 cells.add(entity.getCell());
 *             }
 *         }
 *     }
 *     return cells;
 * }
 */
static int lw_area_allies_get_area(const struct LwMap *map,
                                   LwCell *launchCell, LwCell *targetCell,
                                   struct LwLeek *caster,
                                   LwCell *out_buf, int out_cap) {
    (void)launchCell; (void)targetCell;
    int n = 0;
    if (caster != NULL) {
        int n_entities = 0;
        struct LwLeek **entities = lw_state_get_entities(lw_map_get_state(map), &n_entities);
        for (int i = 0; i < n_entities; i++) {
            struct LwLeek *entity = entities[i];
            if (lw_leek_get_team(entity) == lw_leek_get_team(caster)
                && lw_str_contains(lw_leek_get_name(entity), "crystal")) continue;
            if (lw_leek_get_cell(entity) != NULL && lw_leek_get_team(entity) == lw_leek_get_team(caster)) {
                LwCell *c = lw_leek_get_cell(entity);
                if (n < out_cap) out_buf[n] = *c;
                n++;
            }
        }
    }
    return n;
}


/* ---- AreaEnemies.java --------------------------------------------
 *
 * @Override
 * public List<Cell> getArea(Map map, Cell launchCell, Cell targetCell, Entity caster) {
 *     var cells = new ArrayList<Cell>();
 *     if (caster != null) {
 *         for (var entity : map.getState().getEntities().values()) {
 *             if (entity.getCell() != null && entity.getTeam() != caster.getTeam()) {
 *                 cells.add(entity.getCell());
 *             }
 *         }
 *     }
 *     return cells;
 * }
 */
static int lw_area_enemies_get_area(const struct LwMap *map,
                                    LwCell *launchCell, LwCell *targetCell,
                                    struct LwLeek *caster,
                                    LwCell *out_buf, int out_cap) {
    (void)launchCell; (void)targetCell;
    int n = 0;
    if (caster != NULL) {
        int n_entities = 0;
        struct LwLeek **entities = lw_state_get_entities(lw_map_get_state(map), &n_entities);
        for (int i = 0; i < n_entities; i++) {
            struct LwLeek *entity = entities[i];
            if (lw_leek_get_cell(entity) != NULL && lw_leek_get_team(entity) != lw_leek_get_team(caster)) {
                LwCell *c = lw_leek_get_cell(entity);
                if (n < out_cap) out_buf[n] = *c;
                n++;
            }
        }
    }
    return n;
}


/* Suppress unused-static warnings for lw_abs_i (kept around because
 * the Java sources use Math.abs heavily and future area subclasses
 * may need it). */
static void lw_area_unused_keepers(void) {
    (void)lw_abs_i(0);
}


/* ---- public dispatch --------------------------------------------- */

/* Java: public abstract List<Cell> getArea(...)
 *
 * Switch on the tag, call the matching per-type implementation. */
int lw_area_get_area(const LwArea *self,
                     const struct LwMap *map,
                     LwCell *cast,
                     LwCell *target,
                     struct LwLeek *caster,
                     LwCell *out_buf,
                     int out_cap) {
    if (self == NULL) return 0;
    switch (self->type) {
        case LW_AREA_TAG_SINGLE_CELL:
            return lw_area_single_cell_get_area(map, cast, target, caster, out_buf, out_cap);
        case LW_AREA_TAG_LASER_LINE:
            return lw_area_laser_line_get_area(self->attack, map, cast, target, caster, out_buf, out_cap);
        case LW_AREA_TAG_CIRCLE1:
        case LW_AREA_TAG_CIRCLE2:
        case LW_AREA_TAG_CIRCLE3:
        case LW_AREA_TAG_PLUS_2:
        case LW_AREA_TAG_PLUS_3:
        case LW_AREA_TAG_X_1:
        case LW_AREA_TAG_X_2:
        case LW_AREA_TAG_X_3:
        case LW_AREA_TAG_SQUARE_1:
        case LW_AREA_TAG_SQUARE_2:
            return lw_area_mask_get_area(lw_mask_for_tag(self->type),
                                         map, cast, target, caster, out_buf, out_cap);
        case LW_AREA_TAG_FIRST_IN_LINE:
            return lw_area_first_in_line_get_area(self->attack, map, cast, target, caster, out_buf, out_cap);
        case LW_AREA_TAG_ALLIES:
            return lw_area_allies_get_area(map, cast, target, caster, out_buf, out_cap);
        case LW_AREA_TAG_ENEMIES:
            return lw_area_enemies_get_area(map, cast, target, caster, out_buf, out_cap);
    }
    return 0;
}


/* Java: public static Area getArea(Attack attack, byte type)
 *
 * if (type == Area.TYPE_SINGLE_CELL) return new AreaSingleCell(attack);
 * else if (type == Area.TYPE_LASER_LINE) return new AreaLaserLine(attack);
 * else if (type == Area.TYPE_CIRCLE1 || type == Area.TYPE_AREA_PLUS_1) return new AreaCircle1(attack);
 * else if (type == Area.TYPE_CIRCLE2) return new AreaCircle2(attack);
 * ... etc. ...
 * return null;
 *
 * NOTE: TYPE_AREA_PLUS_1 == TYPE_CIRCLE1 in Java, so the OR clause
 * collapses into the same case here.
 */
int lw_area_get_area_for_type(LwArea *out, const struct LwAttack *attack, int type) {
    if (out == NULL) return 0;
    out->attack = attack;
    if (type == LW_AREA_TYPE_SINGLE_CELL) {
        out->type = LW_AREA_TAG_SINGLE_CELL;
    } else if (type == LW_AREA_TYPE_LASER_LINE) {
        out->type = LW_AREA_TAG_LASER_LINE;
    } else if (type == LW_AREA_TYPE_CIRCLE1 || type == LW_AREA_TYPE_AREA_PLUS_1) {
        out->type = LW_AREA_TAG_CIRCLE1;
    } else if (type == LW_AREA_TYPE_CIRCLE2) {
        out->type = LW_AREA_TAG_CIRCLE2;
    } else if (type == LW_AREA_TYPE_CIRCLE3) {
        out->type = LW_AREA_TAG_CIRCLE3;
    } else if (type == LW_AREA_TYPE_AREA_PLUS_2) {
        out->type = LW_AREA_TAG_PLUS_2;
    } else if (type == LW_AREA_TYPE_AREA_PLUS_3) {
        out->type = LW_AREA_TAG_PLUS_3;
    } else if (type == LW_AREA_TYPE_X_1) {
        out->type = LW_AREA_TAG_X_1;
    } else if (type == LW_AREA_TYPE_X_2) {
        out->type = LW_AREA_TAG_X_2;
    } else if (type == LW_AREA_TYPE_X_3) {
        out->type = LW_AREA_TAG_X_3;
    } else if (type == LW_AREA_TYPE_SQUARE_1) {
        out->type = LW_AREA_TAG_SQUARE_1;
    } else if (type == LW_AREA_TYPE_SQUARE_2) {
        out->type = LW_AREA_TAG_SQUARE_2;
    } else if (type == LW_AREA_TYPE_FIRST_IN_LINE) {
        out->type = LW_AREA_TAG_FIRST_IN_LINE;
    } else if (type == LW_AREA_TYPE_ALLIES) {
        out->type = LW_AREA_TAG_ALLIES;
    } else if (type == LW_AREA_TYPE_ENEMIES) {
        out->type = LW_AREA_TAG_ENEMIES;
    } else {
        return 0;
    }
    /* Touch the unused-keeper to silence -Wunused-function warnings
     * about lw_area_unused_keepers / lw_abs_i. */
    if (type == -999999) lw_area_unused_keepers();
    return 1;
}
