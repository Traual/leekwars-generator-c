/*
 * lw_scenario.c -- 1:1 port of scenario/Scenario.java
 *
 * The Java class is a JSON-fed POD with maps for farmers/teams and a
 * list-of-lists for entities. We dropped the JSON loader (the binding
 * builds the scenario from Python). The TreeMap accessors (.put / .get)
 * collapse to linear scans of dense tables; the entity list-of-lists
 * is a 2D array bounded by LW_SCENARIO_MAX_TEAMS *
 * LW_SCENARIO_MAX_ENTITIES_PER_TEAM.
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/scenario/Scenario.java
 */
#include "lw_scenario.h"

#include <string.h>


/* Java:
 *   private static final FarmerInfo leekwarsFarmer = new FarmerInfo();
 *   static {
 *     leekwarsFarmer.id = 0;
 *     leekwarsFarmer.name = "Leek Wars";
 *     leekwarsFarmer.country = "fr";
 *   }
 */
static const LwFarmerInfo s_leekwars_farmer = {
    .id = 0,
    .name = "Leek Wars",
    .country = "fr",
};


/* Java: public Scenario() {
 *   this.seed = 1 + (int) ((Math.random() * System.nanoTime()) % (Integer.MAX_VALUE));
 * }
 *
 * NOTE: we do NOT replicate the Math.random()-based seed default; the
 * binding always supplies a seed explicitly. Default-init zero-fills. */
void lw_scenario_init(LwScenario *self) {
    if (self == NULL) return;
    memset(self, 0, sizeof(*self));
    self->seed = 0;
    self->max_turns = 64;       /* Java: public int maxTurns = 64; */
    self->type = 0;
    self->context = 0;
    self->fight_id = 0;
    self->boss = 0;
    self->draw_check_life = 0;
}


/* Java: public void addEntity(int teamID, EntityInfo entity) {
 *   if (entity == null || teamID < 0) return;
 *   while (entities.size() < teamID + 1) entities.add(new ArrayList<>());
 *   entities.get(teamID).add(entity);
 * }
 */
void lw_scenario_add_entity(LwScenario *self, int team_id, const LwEntityInfo *entity) {
    if (entity == NULL || team_id < 0) return;
    if (team_id >= LW_SCENARIO_MAX_TEAMS) return;

    /* while (entities.size() < teamID + 1) entities.add(...); */
    if (team_id + 1 > self->n_entity_teams) {
        self->n_entity_teams = team_id + 1;
    }

    int slot = self->n_entities[team_id];
    if (slot >= LW_SCENARIO_MAX_ENTITIES_PER_TEAM) return;

    self->entities[team_id][slot] = *entity;
    self->n_entities[team_id] = slot + 1;
}


/* Java: scenario.farmers.put(farmer.id, farmer);
 *
 * In C: append to the dense table.  If a farmer with the same id
 * already exists, overwrite (Map.put semantic). */
void lw_scenario_add_farmer(LwScenario *self, const LwFarmerInfo *farmer) {
    if (self == NULL || farmer == NULL) return;
    for (int i = 0; i < self->n_farmers; i++) {
        if (self->farmers[i].id == farmer->id) {
            self->farmers[i] = *farmer;
            return;
        }
    }
    if (self->n_farmers >= LW_SCENARIO_MAX_FARMERS) return;
    self->farmers[self->n_farmers++] = *farmer;
}


/* Java: scenario.teams.put(team.id, team); */
void lw_scenario_add_team(LwScenario *self, const LwTeamInfo *team) {
    if (self == NULL || team == NULL) return;
    for (int i = 0; i < self->n_teams; i++) {
        if (self->teams[i].id == team->id) {
            self->teams[i] = *team;
            return;
        }
    }
    if (self->n_teams >= LW_SCENARIO_MAX_TEAM_INFOS) return;
    self->teams[self->n_teams++] = *team;
}


/* Java: public FarmerInfo getFarmer(int farmer) {
 *   if (farmer == 0) return leekwarsFarmer;
 *   else return this.farmers.get(farmer);
 * }
 */
const LwFarmerInfo* lw_scenario_get_farmer(const LwScenario *self, int farmer) {
    if (self == NULL) return NULL;
    if (farmer == 0) {
        return &s_leekwars_farmer;
    }
    for (int i = 0; i < self->n_farmers; i++) {
        if (self->farmers[i].id == farmer) {
            return &self->farmers[i];
        }
    }
    return NULL;
}


/* Java: scenario.teams.get(team)  (used internally by EntityInfo.createEntity). */
const LwTeamInfo* lw_scenario_get_team(const LwScenario *self, int team) {
    if (self == NULL) return NULL;
    for (int i = 0; i < self->n_teams; i++) {
        if (self->teams[i].id == team) {
            return &self->teams[i];
        }
    }
    return NULL;
}


/* Java: public void setEntityAI(int team, int leek_id, String aiName,
 *                               int aiFolder, String aiPath, int aiOwner,
 *                               int aiVersion, boolean aiStrict) {
 *   for (EntityInfo entity : entities.get(team)) {
 *     if (entity.id == leek_id) {
 *       entity.ai = aiName;
 *       entity.ai_folder = aiFolder;
 *       entity.ai_path = aiPath;
 *       entity.aiOwner = aiOwner;
 *       entity.ai_version = aiVersion;
 *       entity.ai_strict = aiStrict;
 *     }
 *   }
 * }
 */
void lw_scenario_set_entity_ai(LwScenario *self, int team, int leek_id,
                               const char *ai_name, int ai_folder,
                               const char *ai_path, int ai_owner,
                               int ai_version, int ai_strict) {
    if (self == NULL) return;
    if (team < 0 || team >= self->n_entity_teams) return;

    for (int i = 0; i < self->n_entities[team]; i++) {
        LwEntityInfo *entity = &self->entities[team][i];
        if (entity->id == leek_id) {
            if (ai_name != NULL) {
                strncpy(entity->ai, ai_name, sizeof(entity->ai) - 1);
                entity->ai[sizeof(entity->ai) - 1] = '\0';
            }
            entity->ai_folder = ai_folder;
            if (ai_path != NULL) {
                strncpy(entity->ai_path, ai_path, sizeof(entity->ai_path) - 1);
                entity->ai_path[sizeof(entity->ai_path) - 1] = '\0';
            }
            entity->aiOwner = ai_owner;
            entity->ai_version = ai_version;
            entity->ai_strict = ai_strict ? 1 : 0;
        }
    }
}
