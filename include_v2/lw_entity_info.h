/*
 * lw_entity_info.h -- 1:1 port of scenario/EntityInfo.java
 *                       (also contains FarmerInfo + TeamInfo since they
 *                        are tiny POD structs in the same package).
 *
 * Java fields → C struct fields, snake_case for the C field name where
 * the Java identifier is camelCase, otherwise verbatim.
 *
 * The Java entity is built from a JSON ObjectNode in the constructor.
 * We don't port the JSON deserialiser here -- callers fill the struct
 * fields directly (the Cython binding constructs LwEntityInfo from
 * Python dicts).
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/scenario/EntityInfo.java
 *   java_reference/src/main/java/com/leekwars/generator/scenario/FarmerInfo.java
 *   java_reference/src/main/java/com/leekwars/generator/scenario/TeamInfo.java
 */
#ifndef LW_ENTITY_INFO_H
#define LW_ENTITY_INFO_H

#include "lw_constants.h"

/* Forward declarations -- ported in parallel. */
struct LwState;
struct LwEntity;
struct LwGenerator;
struct LwScenario;
struct LwFight;


/* ----- bounds for char[] string fields --------------------------------
 * Java uses java.lang.String (unbounded). We bound the strings here at
 * sizes large enough for any leek/turret/farmer name observed in the
 * wild. The numbers mirror the Python upstream.
 */
#define LW_NAME_MAX        64
#define LW_AI_PATH_MAX    256
#define LW_COUNTRY_MAX     16
#define LW_FARMER_NAME_MAX 64
#define LW_TEAM_NAME_MAX   64
#define LW_COMP_NAME_MAX   64

/* Bounds for inventory arrays (weapons / chips). The Java ArrayList is
 * unbounded but practical leeks max out around a dozen of each. */
#define LW_ENTITY_INFO_MAX_WEAPONS 16
#define LW_ENTITY_INFO_MAX_CHIPS   32


/* ----- FarmerInfo ----------------------------------------------------- */

/* Java:
 *   public class FarmerInfo {
 *       public int id;
 *       public String name;
 *       public String country;
 *   }
 */
typedef struct {
    int  id;
    char name[LW_FARMER_NAME_MAX];
    char country[LW_COUNTRY_MAX];
} LwFarmerInfo;


/* ----- TeamInfo ------------------------------------------------------- */

/* Java:
 *   public class TeamInfo {
 *       public int id;
 *       public String name = "";
 *       public String compositionName = null;
 *       public int level;
 *       public String turretAIPath;
 *       public int turretAIOwner;
 *   }
 *
 * NOTE: composition_name == "" means "no composition name" (the Java
 * source sets it to null and only emits when non-null). turretAIPath
 * is unused by the engine itself -- it is consumed by the Java AI
 * resolver, which we replace with a Python callback. */
typedef struct {
    int  id;
    char name[LW_TEAM_NAME_MAX];
    char composition_name[LW_COMP_NAME_MAX];
    int  has_composition_name;     /* 0 = composition_name is null */
    int  level;
    char turret_ai_path[LW_AI_PATH_MAX];
    int  turret_ai_owner;
} LwTeamInfo;


/* ----- EntityInfo ----------------------------------------------------- */

/* Java:
 *   static private final Class<?> classes[] = {
 *       Leek.class,    // type 0
 *       Bulb.class,    // type 1
 *       Turret.class,  // type 2
 *   };
 *
 * Mapped to the LW_ENTITY_TYPE_* constants from lw_constants.h:
 *   classes[0] = LW_ENTITY_TYPE_LEEK
 *   classes[1] = LW_ENTITY_TYPE_BULB
 *   classes[2] = LW_ENTITY_TYPE_TURRET
 */
typedef struct {
    int  id;
    char name[LW_NAME_MAX];

    /* AI resolver fields. The Java side resolves these via reflection
     * + the LeekScript file system. In C we keep the path / metadata
     * and hand them to a callback supplied by the binding (see ai_fn
     * below). */
    char ai[LW_AI_PATH_MAX];        /* Java: public String ai */
    int  ai_folder;                 /* Java: public int ai_folder */
    char ai_path[LW_AI_PATH_MAX];   /* Java: public String ai_path */
    int  ai_version;                /* Java: public int ai_version */
    int  ai_strict;                 /* Java: public boolean ai_strict */
    int  aiOwner;                   /* Java: public int aiOwner */

    int  type;                      /* LW_ENTITY_TYPE_* */
    int  farmer;
    int  team;
    int  level;
    int  dead;                      /* boolean -> int (0/1) */
    int  life;
    int  tp;
    int  mp;
    int  strength;
    int  agility;
    int  frequency;
    int  wisdom;
    int  resistance;
    int  science;
    int  magic;
    int  cores;
    int  ram;

    int  chips[LW_ENTITY_INFO_MAX_CHIPS];
    int  n_chips;
    int  weapons[LW_ENTITY_INFO_MAX_WEAPONS];
    int  n_weapons;

    /* Java: public Integer cell;  (Integer => may be null)
     * In C we use a pair (cell, has_cell) since 0 is a legal cell id. */
    int  cell;
    int  has_cell;

    int  skin;
    int  hat;
    int  metal;                     /* boolean -> int */
    int  face;

    /* Java: public Class<?> customClass;
     * Used to override the entity-class lookup for tests. In C we use
     * the same LW_ENTITY_TYPE_* tag in `type` and let custom_class > 0
     * mean "use that type instead". 0 means "no override". */
    int  custom_class;

    int  orientation;

    /* Opaque AI handle. The Java code stores a leekscript AIFile here
     * (the parsed AI bytecode). In C we just carry an opaque pointer
     * that the AI dispatcher (Python callback or function pointer)
     * understands. NULL means "no AI". */
    void *ai_file;
} LwEntityInfo;


/* ---- API -------------------------------------------------------------- */

/* public EntityInfo() {} -- field defaults */
void lw_entity_info_init(LwEntityInfo *self);

/* public Entity createEntity(Generator generator, Scenario scenario, Fight fight)
 *
 * Allocates and configures an LwEntity from this LwEntityInfo. Returns
 * NULL on failure (matches Java's catch block). The new entity is
 * pushed via state.addEntity() by the caller; this function only fills
 * the entity's fields.
 *
 * NOTE: the Java reflection-based constructor selection collapses to
 * a switch on `type` (LEEK/BULB/TURRET), since custom_class is unused
 * outside tests.
 */
struct LwEntity* lw_entity_info_create_entity(const LwEntityInfo *self,
                                              struct LwGenerator *generator,
                                              struct LwScenario *scenario,
                                              struct LwFight *fight);


#endif /* LW_ENTITY_INFO_H */
