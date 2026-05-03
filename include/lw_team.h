/*
 * lw_team.h -- 1:1 port of state/Team.java
 *
 * Java:   public class Team {
 *             private int id;
 *             private final List<Entity> entities;
 *             private final TreeMap<Integer,Integer> cooldowns;
 *             private final HashSet<Integer> flags;
 *         }
 *
 * In C, the entities[] array stores LwEntity pointers (Team does not
 * own the entity storage; LwState does). Cooldowns mirror the int->int
 * TreeMap used on Entity, kept sorted by key ascending. Flags are kept
 * as a sorted int array for HashSet-equivalent membership tests.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/Team.java
 */
#ifndef LW_TEAM_H
#define LW_TEAM_H

#include "lw_entity.h"

/* Forward decl -- the State pointer used by the copy ctor. */
struct LwState;
struct LwChip;


/* Capacities -- Java has no fixed cap. Sized to comfortably fit the
 * largest fights (battle royale up to 8 farmers x 8 leeks x ~3 summons). */
#define LW_TEAM_MAX_ENTITIES   64
#define LW_TEAM_MAX_COOLDOWNS  32
#define LW_TEAM_MAX_FLAGS      16


typedef struct {

    int                 id;                          /* private int id */

    /* private final List<Entity> entities */
    LwEntity           *entities[LW_TEAM_MAX_ENTITIES];
    int                 n_entities;

    /* private final TreeMap<Integer,Integer> cooldowns -- same shape as
     * Entity's mCooldown. We reuse the LwIntIntEntry type from lw_entity.h. */
    LwIntIntEntry       cooldowns[LW_TEAM_MAX_COOLDOWNS];
    int                 n_cooldowns;

    /* private final HashSet<Integer> flags -- sorted ascending. */
    int                 flags[LW_TEAM_MAX_FLAGS];
    int                 n_flags;

} LwTeam;


/* Java: public Team() {
 *           entities = new ArrayList<Entity>();
 *           cooldowns = new TreeMap<Integer, Integer>();
 *           flags = new HashSet<Integer>();
 *       }
 */
void lw_team_init(LwTeam *self);

/* Java: public Team(Team team, State state) {
 *           // entities are looked up in the new state by fid
 *       }
 */
void lw_team_init_copy(LwTeam *self, const LwTeam *team, struct LwState *state);

/* Java: public int getID() / setID(int) */
int  lw_team_get_id(const LwTeam *self);
void lw_team_set_id(LwTeam *self, int id);

/* Java: public List<Entity> getEntities()  -- iteration helpers. */
int       lw_team_get_entities_count(const LwTeam *self);
LwEntity* lw_team_get_entity_at      (const LwTeam *self, int index);

/* Java: public void addEntity(Entity entity) */
void lw_team_add_entity(LwTeam *self, LwEntity *entity);

/* Java: public void removeEntity(Entity invoc) */
void lw_team_remove_entity(LwTeam *self, LwEntity *invoc);

/* Java: public boolean isDead() / isAlive() */
int  lw_team_is_dead (const LwTeam *self);
int  lw_team_is_alive(const LwTeam *self);

/* Java: public int size() */
int  lw_team_size    (const LwTeam *self);

/* Java: public void addCooldown(Chip chip, int cooldown) */
void lw_team_add_cooldown(LwTeam *self, struct LwChip *chip, int cooldown);

/* Java: public boolean hasCooldown(int chipID) */
int  lw_team_has_cooldown(const LwTeam *self, int chip_id);

/* Java: public int getCooldown(int chipID) */
int  lw_team_get_cooldown(const LwTeam *self, int chip_id);

/* Java: public Map<Integer,Integer> getCooldowns() -- iteration helpers. */
int  lw_team_get_cooldowns_count(const LwTeam *self);
int  lw_team_get_cooldown_key_at(const LwTeam *self, int index);
int  lw_team_get_cooldown_value_at(const LwTeam *self, int index);

/* Java: public void applyCoolDown() */
void lw_team_apply_cool_down(LwTeam *self);

/* Java: public int getSummonCount() */
int  lw_team_get_summon_count(const LwTeam *self);

/* Java: public double getDeadRatio() */
double lw_team_get_dead_ratio(const LwTeam *self);

/* Java: public double getLifeRatio() */
double lw_team_get_life_ratio(const LwTeam *self);

/* Java: public boolean containsChest() */
int  lw_team_contains_chest(const LwTeam *self);

/* Java: public int getLife() */
int  lw_team_get_life(const LwTeam *self);

/* Java: public HashSet<Integer> getFlags() / addFlag(int) */
int  lw_team_get_flags_count (const LwTeam *self);
int  lw_team_get_flag_at     (const LwTeam *self, int index);
void lw_team_add_flag        (LwTeam *self, int flag);


#endif /* LW_TEAM_H */
