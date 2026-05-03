/*
 * lw_team.c -- 1:1 port of state/Team.java
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/Team.java
 */
#include "lw_team.h"
#include "lw_constants.h"
#include "lw_state.h"
#include "lw_chip.h"

#include <string.h>


/* All required engine APIs (lw_state_get_entity, lw_chip_get_id) come
 * from the headers included above. */


/* MAX_TURNS constant (state/State.MAX_TURNS) */
#define LW_TEAM_MAX_TURNS_PLUS_2  (LW_FIGHT_MAX_TURNS + 2)


/* ---- TreeMap-equivalent helpers (sorted ascending) --------------- */

static int lw_team_iie_find(const LwIntIntEntry *arr, int n, int key) {
    for (int i = 0; i < n; i++) {
        if (arr[i].present && arr[i].key == key) return i;
    }
    return -1;
}

static void lw_team_iie_put(LwIntIntEntry *arr, int *pn, int cap, int key, int value) {
    int idx = lw_team_iie_find(arr, *pn, key);
    if (idx >= 0) {
        arr[idx].value = value;
        return;
    }
    int pos = *pn;
    for (int i = 0; i < *pn; i++) {
        if (key < arr[i].key) { pos = i; break; }
    }
    if (*pn >= cap) return;
    for (int i = *pn; i > pos; i--) arr[i] = arr[i - 1];
    arr[pos].key = key;
    arr[pos].value = value;
    arr[pos].present = 1;
    (*pn)++;
}

static void lw_team_iie_remove(LwIntIntEntry *arr, int *pn, int key) {
    int idx = lw_team_iie_find(arr, *pn, key);
    if (idx < 0) return;
    for (int i = idx; i < *pn - 1; i++) arr[i] = arr[i + 1];
    (*pn)--;
    arr[*pn].present = 0;
}


/* ---- Constructors ------------------------------------------------ */

/* Java: public Team() {
 *           entities = new ArrayList<Entity>();
 *           cooldowns = new TreeMap<Integer, Integer>();
 *           flags = new HashSet<Integer>();
 *       }
 */
void lw_team_init(LwTeam *self) {
    memset(self, 0, sizeof(*self));
}

/* Java: public Team(Team team, State state) {
 *           this.entities = new ArrayList<Entity>();
 *           for (var entity : team.entities) {
 *               this.entities.add(state.getEntity(entity.getFId()));
 *           }
 *           this.cooldowns = new TreeMap<Integer, Integer>(team.cooldowns);
 *           this.flags = new HashSet<Integer>(team.flags);
 *       }
 */
void lw_team_init_copy(LwTeam *self, const LwTeam *team, struct LwState *state) {
    memset(self, 0, sizeof(*self));
    self->id = team->id;

    self->n_entities = team->n_entities;
    for (int i = 0; i < team->n_entities; i++) {
        LwEntity *src = team->entities[i];
        self->entities[i] = lw_state_get_entity(state, lw_entity_get_fid(src));
    }

    self->n_cooldowns = team->n_cooldowns;
    for (int i = 0; i < team->n_cooldowns; i++) {
        self->cooldowns[i] = team->cooldowns[i];
    }

    self->n_flags = team->n_flags;
    for (int i = 0; i < team->n_flags; i++) {
        self->flags[i] = team->flags[i];
    }
}


/* ---- ID ---------------------------------------------------------- */

int  lw_team_get_id(const LwTeam *self) { return self->id; }
void lw_team_set_id(LwTeam *self, int id) { self->id = id; }


/* ---- Entity list ------------------------------------------------- */

int       lw_team_get_entities_count(const LwTeam *self) { return self->n_entities; }

LwEntity* lw_team_get_entity_at(const LwTeam *self, int index) {
    if (index < 0 || index >= self->n_entities) return NULL;
    return self->entities[index];
}

/* Java: public void addEntity(Entity entity) { entities.add(entity); } */
void lw_team_add_entity(LwTeam *self, LwEntity *entity) {
    if (self->n_entities >= LW_TEAM_MAX_ENTITIES) return;
    self->entities[self->n_entities++] = entity;
}

/* Java: public void removeEntity(Entity invoc) { entities.remove(invoc); } */
void lw_team_remove_entity(LwTeam *self, LwEntity *invoc) {
    int idx = -1;
    for (int i = 0; i < self->n_entities; i++) {
        if (self->entities[i] == invoc) { idx = i; break; }
    }
    if (idx < 0) return;
    for (int i = idx; i < self->n_entities - 1; i++) {
        self->entities[i] = self->entities[i + 1];
    }
    self->n_entities--;
    self->entities[self->n_entities] = NULL;
}


/* ---- Dead / alive ----------------------------------------------- */

/* Java: public boolean isDead() {
 *     for (Entity entity : entities) {
 *         // The team is dead if the turret is dead
 *         if (entity.getType() == Entity.TYPE_TURRET && entity.isDead()) {
 *             return true;
 *         }
 *     }
 *     for (Entity entity : entities) {
 *         // The team is not dead if there is an alive leek
 *         if (entity.getType() != Entity.TYPE_TURRET && !entity.isDead()) {
 *             return false;
 *         }
 *     }
 *     return true;
 * }
 */
int lw_team_is_dead(const LwTeam *self) {
    for (int i = 0; i < self->n_entities; i++) {
        LwEntity *entity = self->entities[i];
        if (lw_entity_get_type(entity) == LW_ENTITY_TYPE_TURRET && lw_entity_is_dead(entity)) {
            return 1;
        }
    }
    for (int i = 0; i < self->n_entities; i++) {
        LwEntity *entity = self->entities[i];
        if (lw_entity_get_type(entity) != LW_ENTITY_TYPE_TURRET && !lw_entity_is_dead(entity)) {
            return 0;
        }
    }
    return 1;
}

/* Java: public boolean isAlive() { return !isDead(); } */
int lw_team_is_alive(const LwTeam *self) { return !lw_team_is_dead(self); }


/* ---- size ------------------------------------------------------- */

/* Java: public int size() { return entities.size(); } */
int lw_team_size(const LwTeam *self) { return self->n_entities; }


/* ---- Cooldowns -------------------------------------------------- */

/* Java: public void addCooldown(Chip chip, int cooldown) {
 *     cooldowns.put(chip.getId(), cooldown == -1 ? State.MAX_TURNS + 2 : cooldown);
 * }
 */
void lw_team_add_cooldown(LwTeam *self, struct LwChip *chip, int cooldown) {
    int v = (cooldown == -1) ? LW_TEAM_MAX_TURNS_PLUS_2 : cooldown;
    lw_team_iie_put(self->cooldowns, &self->n_cooldowns,
                    LW_TEAM_MAX_COOLDOWNS, lw_chip_get_id(chip), v);
}

/* Java: public boolean hasCooldown(int chipID) { return cooldowns.containsKey(chipID); } */
int lw_team_has_cooldown(const LwTeam *self, int chip_id) {
    return lw_team_iie_find(self->cooldowns, self->n_cooldowns, chip_id) >= 0 ? 1 : 0;
}

/* Java: public int getCooldown(int chipID) {
 *     if (!hasCooldown(chipID)) return 0;
 *     return cooldowns.get(chipID);
 * }
 */
int lw_team_get_cooldown(const LwTeam *self, int chip_id) {
    int idx = lw_team_iie_find(self->cooldowns, self->n_cooldowns, chip_id);
    if (idx < 0) return 0;
    return self->cooldowns[idx].value;
}

int lw_team_get_cooldowns_count(const LwTeam *self) { return self->n_cooldowns; }

int lw_team_get_cooldown_key_at(const LwTeam *self, int index) {
    if (index < 0 || index >= self->n_cooldowns) return 0;
    return self->cooldowns[index].key;
}

int lw_team_get_cooldown_value_at(const LwTeam *self, int index) {
    if (index < 0 || index >= self->n_cooldowns) return 0;
    return self->cooldowns[index].value;
}


/* Java: public void applyCoolDown() {
 *     Map<Integer, Integer> cooldown = new TreeMap<Integer, Integer>();
 *     cooldown.putAll(cooldowns);
 *     for (Entry<Integer, Integer> chip : cooldown.entrySet()) {
 *         if (chip.getValue() <= 1)
 *             cooldowns.remove(chip.getKey());
 *         else
 *             cooldowns.put(chip.getKey(), chip.getValue() - 1);
 *     }
 * }
 */
void lw_team_apply_cool_down(LwTeam *self) {
    LwIntIntEntry snapshot[LW_TEAM_MAX_COOLDOWNS];
    int n_snapshot = self->n_cooldowns;
    for (int i = 0; i < n_snapshot; i++) snapshot[i] = self->cooldowns[i];

    for (int i = 0; i < n_snapshot; i++) {
        int key = snapshot[i].key;
        int val = snapshot[i].value;
        if (val <= 1) {
            lw_team_iie_remove(self->cooldowns, &self->n_cooldowns, key);
        } else {
            lw_team_iie_put(self->cooldowns, &self->n_cooldowns,
                            LW_TEAM_MAX_COOLDOWNS, key, val - 1);
        }
    }
}


/* ---- Summon / dead ratios --------------------------------------- */

/* Java: public int getSummonCount() {
 *     int nb = 0;
 *     for (Entity e : entities) {
 *         if (!e.isDead() && e.isSummon()) {
 *             nb++;
 *         }
 *     }
 *     return nb;
 * }
 */
int lw_team_get_summon_count(const LwTeam *self) {
    int nb = 0;
    for (int i = 0; i < self->n_entities; i++) {
        LwEntity *e = self->entities[i];
        if (!lw_entity_is_dead(e) && lw_entity_is_summon(e)) {
            nb++;
        }
    }
    return nb;
}

/* Java: public double getDeadRatio() {
 *     int dead = 0;
 *     int total = 0;
 *     for (Entity entity : entities) {
 *         if (entity.isSummon()) continue;
 *         total++;
 *         if (entity.isDead()) dead++;
 *     }
 *     return (double) dead / total;
 * }
 */
double lw_team_get_dead_ratio(const LwTeam *self) {
    int dead = 0;
    int total = 0;
    for (int i = 0; i < self->n_entities; i++) {
        LwEntity *entity = self->entities[i];
        if (lw_entity_is_summon(entity)) continue;
        total++;
        if (lw_entity_is_dead(entity)) dead++;
    }
    /* Java throws ArithmeticException if total == 0; guard defensively. */
    if (total == 0) return 0.0;
    return (double)dead / (double)total;
}

/* Java: public double getLifeRatio() {
 *     int life = 0;
 *     int total = 0;
 *     for (Entity entity : entities) {
 *         if (entity.isSummon()) continue;
 *         if (entity instanceof Turret) continue;
 *         total += entity.getTotalLife();
 *         life += entity.getLife();
 *     }
 *     return (double) life / total;
 * }
 */
double lw_team_get_life_ratio(const LwTeam *self) {
    int life = 0;
    int total = 0;
    for (int i = 0; i < self->n_entities; i++) {
        LwEntity *entity = self->entities[i];
        if (lw_entity_is_summon(entity)) continue;
        if (lw_entity_get_type(entity) == LW_ENTITY_TYPE_TURRET) continue;
        total += lw_entity_get_total_life(entity);
        life += lw_entity_get_life(entity);
    }
    if (total == 0) return 0.0;
    return (double)life / (double)total;
}


/* Java: public boolean containsChest() {
 *     for (var entity : entities) {
 *         if (entity.getType() == Entity.TYPE_CHEST) return true;
 *     }
 *     return false;
 * }
 */
int lw_team_contains_chest(const LwTeam *self) {
    for (int i = 0; i < self->n_entities; i++) {
        if (lw_entity_get_type(self->entities[i]) == LW_ENTITY_TYPE_CHEST) return 1;
    }
    return 0;
}


/* Java: public int getLife() {
 *     int life = 0;
 *     for (Entity entity : entities) {
 *         if (entity.isSummon()) continue;
 *         if (entity instanceof Turret) continue;
 *         life += entity.getLife();
 *     }
 *     return life;
 * }
 */
int lw_team_get_life(const LwTeam *self) {
    int life = 0;
    for (int i = 0; i < self->n_entities; i++) {
        LwEntity *entity = self->entities[i];
        if (lw_entity_is_summon(entity)) continue;
        if (lw_entity_get_type(entity) == LW_ENTITY_TYPE_TURRET) continue;
        life += lw_entity_get_life(entity);
    }
    return life;
}


/* ---- Flags ------------------------------------------------------ */

int lw_team_get_flags_count(const LwTeam *self) { return self->n_flags; }

int lw_team_get_flag_at(const LwTeam *self, int index) {
    if (index < 0 || index >= self->n_flags) return 0;
    return self->flags[index];
}

/* Java: public void addFlag(int flag) { flags.add(flag); } */
void lw_team_add_flag(LwTeam *self, int flag) {
    /* HashSet semantics: ignore duplicates. */
    for (int i = 0; i < self->n_flags; i++) {
        if (self->flags[i] == flag) return;
    }
    if (self->n_flags >= LW_TEAM_MAX_FLAGS) return;
    /* Insert sorted (so iteration is deterministic). */
    int pos = self->n_flags;
    for (int i = 0; i < self->n_flags; i++) {
        if (flag < self->flags[i]) { pos = i; break; }
    }
    for (int i = self->n_flags; i > pos; i--) {
        self->flags[i] = self->flags[i - 1];
    }
    self->flags[pos] = flag;
    self->n_flags++;
}
