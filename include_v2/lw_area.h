/*
 * lw_area.h -- 1:1 port of area/Area.java + all 16 subclasses.
 *
 * Java has an abstract base class `Area` with one virtual method
 *
 *     public abstract List<Cell> getArea(Map map, Cell launchCell,
 *                                        Cell targetCell, Entity caster);
 *
 * and 16 concrete subclasses (AreaSingleCell, AreaCircle1..3,
 * AreaSquare1..2, AreaPlus2..3, AreaX1..3, AreaLaserLine,
 * AreaFirstInLine, AreaAllies, AreaEnemies, plus the MaskArea base
 * for the mask-driven shapes).
 *
 * In C this becomes a tag-and-switch dispatch (LwAreaType + a
 * lw_area_get_area() function that dispatches to a static per-type
 * implementation in lw_area.c).
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/area/Area.java
 *   java_reference/src/main/java/com/leekwars/generator/area/Area*.java
 *   java_reference/src/main/java/com/leekwars/generator/area/MaskArea.java
 */
#ifndef LW_AREA_H
#define LW_AREA_H

#include "lw_cell.h"

/* Forward decls -- Map / Attack / Leek live in their own headers
 * (some not yet ported). We only need the pointer types here. */
struct LwMap;
struct LwAttack;
struct LwLeek;


/* ---- Java: Area type constants ---------------------------------- */
/* public final static int TYPE_SINGLE_CELL  = 1;
 * public final static int TYPE_LASER_LINE   = 2;
 * public final static int TYPE_CIRCLE1      = 3;
 * public final static int TYPE_CIRCLE2      = 4;
 * public final static int TYPE_CIRCLE3      = 5;
 * public final static int TYPE_AREA_PLUS_1  = 3; // Equals to CIRCLE_1
 * public final static int TYPE_AREA_PLUS_2  = 6;
 * public final static int TYPE_AREA_PLUS_3  = 7;
 * public final static int TYPE_X_1          = 8;
 * public final static int TYPE_X_2          = 9;
 * public final static int TYPE_X_3          = 10;
 * public final static int TYPE_SQUARE_1     = 11;
 * public final static int TYPE_SQUARE_2     = 12;
 * public final static int TYPE_FIRST_IN_LINE = 13;
 * public final static int TYPE_ENEMIES      = 14;
 * public final static int TYPE_ALLIES       = 15;
 *
 * NOTE: Values match Java exactly. AREA_PLUS_1 aliases CIRCLE1 (3) in
 * Java; we keep the same alias.
 */
#define LW_AREA_TYPE_SINGLE_CELL    1
#define LW_AREA_TYPE_LASER_LINE     2
#define LW_AREA_TYPE_CIRCLE1        3
#define LW_AREA_TYPE_CIRCLE2        4
#define LW_AREA_TYPE_CIRCLE3        5
#define LW_AREA_TYPE_AREA_PLUS_1    3   /* alias of CIRCLE1 */
#define LW_AREA_TYPE_AREA_PLUS_2    6
#define LW_AREA_TYPE_AREA_PLUS_3    7
#define LW_AREA_TYPE_X_1            8
#define LW_AREA_TYPE_X_2            9
#define LW_AREA_TYPE_X_3           10
#define LW_AREA_TYPE_SQUARE_1      11
#define LW_AREA_TYPE_SQUARE_2      12
#define LW_AREA_TYPE_FIRST_IN_LINE 13
#define LW_AREA_TYPE_ENEMIES       14
#define LW_AREA_TYPE_ALLIES        15


/* Tag enum used for the dispatch switch. Values are NOT the Java
 * constants (those have an alias collision: AREA_PLUS_1 == CIRCLE1).
 * Use lw_area_get_area_for_type() to convert from a Java byte type
 * to a populated LwArea. */
typedef enum {
    LW_AREA_TAG_SINGLE_CELL    = 0,
    LW_AREA_TAG_LASER_LINE     = 1,
    LW_AREA_TAG_CIRCLE1        = 2,
    LW_AREA_TAG_CIRCLE2        = 3,
    LW_AREA_TAG_CIRCLE3        = 4,
    LW_AREA_TAG_PLUS_2         = 5,
    LW_AREA_TAG_PLUS_3         = 6,
    LW_AREA_TAG_X_1            = 7,
    LW_AREA_TAG_X_2            = 8,
    LW_AREA_TAG_X_3            = 9,
    LW_AREA_TAG_SQUARE_1       = 10,
    LW_AREA_TAG_SQUARE_2       = 11,
    LW_AREA_TAG_FIRST_IN_LINE  = 12,
    LW_AREA_TAG_ENEMIES        = 13,
    LW_AREA_TAG_ALLIES         = 14
} LwAreaType;


/* Java: protected int mId; protected Attack mAttack;
 *
 * The dispatch tag replaces the subclass identity. mId is private and
 * never read by anything we port (it's a debug field), so we leave it
 * out. mAttack is needed by AreaLaserLine and AreaFirstInLine for
 * range/los queries -- store the pointer here. */
typedef struct {
    LwAreaType type;
    const struct LwAttack *attack;   /* Java: protected Attack mAttack */
} LwArea;


/* public static Area getArea(Attack attack, byte type)
 *
 * Factory: populate `*out` from a Java-side byte type. Returns 1 on
 * success, 0 if the type is unknown (Java returns null). */
int lw_area_get_area_for_type(LwArea *out, const struct LwAttack *attack, int type);


/* public abstract List<Cell> getArea(Map map, Cell launchCell,
 *                                    Cell targetCell, Entity caster);
 *
 * Dispatches to the per-type implementation. Writes up to out_cap
 * cells into out_buf and returns the number written. If out_buf is
 * too small, the function returns the number that would have fit
 * (callers should pass a comfortable upper bound -- the largest
 * shape (CIRCLE3 or SQUARE2) produces fewer than 26 cells; Allies/
 * Enemies bound is the entity count).
 */
int lw_area_get_area(const LwArea *self,
                     const struct LwMap *map,
                     LwCell *cast,
                     LwCell *target,
                     struct LwLeek *caster,
                     LwCell *out_buf,
                     int out_cap);


/* protected boolean isAvailable(Cell c, List<Cell> cells_to_ignore)
 *
 * Helper from the Java Area base class. Not used by any of the ported
 * subclasses (they call Cell.isWalkable / Map.available directly), so
 * we don't expose it -- kept here as a comment for completeness. */


#endif /* LW_AREA_H */
