/*
 * lw_generator.h -- 1:1 port of Generator.java
 *
 * Java's Generator owns:
 *   - The static data tables (weapons, chips, summons, components),
 *     populated from JSON files in data/ at construction time.
 *   - The error manager.
 *   - runScenario() which builds a Fight from a Scenario, runs it, and
 *     returns an Outcome.
 *
 * In C the static tables live in their own modules (lw_weapons.c,
 * lw_chips.c, lw_bulbs.c, lw_components.c). The Generator struct here
 * just carries the engine-wide knobs (cache flag, error manager hook,
 * generator-id). The data-loading helpers stay as method-equivalents
 * so the binding can call them once at startup.
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/Generator.java
 */
#ifndef LW_GENERATOR_H
#define LW_GENERATOR_H

#include "lw_constants.h"
#include "lw_outcome.h"
#include "lw_scenario.h"
#include "lw_fight.h"


/* Forward declarations. */
struct LwState;
struct LwEntity;
struct LwFightListener;
struct LwRegisterManager;
struct LwStatisticsManager;


typedef struct LwGenerator {
    /* Java: public boolean use_leekscript_cache = true; */
    int use_leekscript_cache;

    /* In Java the error manager is a static field; in C we attach the
     * binding's callback to the generator instance for cleanliness. */
    void (*error_handler)(void *userdata, const char *message);
    void  *error_handler_userdata;

    /* AI dispatcher inherited by the Fight (binding-supplied). */
    lw_fight_ai_dispatch_t ai_dispatch;
    void                  *ai_dispatch_userdata;
} LwGenerator;


/* Java: public Generator() {
 *   new File("ai/").mkdir();
 *   LeekFunctions.setExtraFunctions(...);
 *   LeekConstants.setExtraConstants(...);
 *   loadWeapons(); loadChips(); loadSummons(); loadComponents();
 * }
 *
 * In C: just initialise the struct.  Data-loading is exposed as
 * separate functions so the binding can call them with paths it owns.
 */
void lw_generator_init(LwGenerator *self);


/* Optional: install the AI dispatcher used by Fight. */
void lw_generator_set_ai_dispatch(LwGenerator *self,
                                  lw_fight_ai_dispatch_t fn,
                                  void *userdata);


/* Java: private void loadWeapons() {
 *   Json.parseObject(Util.readFile("data/weapons.json")) ...
 *   for each weapon: Weapons.addWeapon(new Weapon(...));
 * }
 *
 * The C engine doesn't parse JSON; the binding loads the file and
 * registers each weapon via lw_weapons_add_weapon().  These helpers
 * exist so the binding can wrap them in a single bootstrap call. */
void lw_generator_load_weapons   (LwGenerator *self);
void lw_generator_load_chips     (LwGenerator *self);
void lw_generator_load_summons   (LwGenerator *self);
void lw_generator_load_components(LwGenerator *self);


/* Java: public void setCache(boolean cache) { this.use_leekscript_cache = cache; } */
void lw_generator_set_cache(LwGenerator *self, int cache);


/* Java: public void exception(Throwable e) { ... } */
void lw_generator_exception        (LwGenerator *self, const char *message);
void lw_generator_exception_in_fight(LwGenerator *self, struct LwFight *fight, const char *message);


/* Java: public Outcome runScenario(Scenario scenario, FightListener listener,
 *                                  RegisterManager registerManager,
 *                                  StatisticsManager statisticsManager)
 *
 * In C the caller pre-allocates the LwOutcome (so we can return a
 * pointer with a stable lifetime). The function returns 0 on success,
 * non-zero on error (the error message is written to outcome.exception).
 *
 * NOTE: the Java engine constructs the Fight + State on the stack /
 * heap inside this function. In C, both are owned by the LwOutcome's
 * `fight` action stream + a caller-supplied LwState/LwFight (we
 * allocate them here when state == NULL).
 */
int  lw_generator_run_scenario(LwGenerator *self,
                               LwScenario *scenario,
                               struct LwFightListener *listener,
                               struct LwRegisterManager *register_manager,
                               struct LwStatisticsManager *statistics_manager,
                               struct LwState *state,         /* caller-owned, zeroed */
                               LwFight *fight,                /* caller-owned, zeroed */
                               LwOutcome *outcome);            /* caller-owned, zeroed */


#endif /* LW_GENERATOR_H */
