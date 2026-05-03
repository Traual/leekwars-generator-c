/*
 * lw_actions.h -- 1:1 port of action/Actions.java + each action/ActionXxx.java
 *
 * Java has one class per action subtype (ActionStartFight, ActionEndTurn,
 * ActionDamage, etc.).  Each one extends the Action interface and ships
 * its own getJSON() that emits an ArrayNode of mixed-type values starting
 * with the Action.* type id.
 *
 * In C we replace the polymorphic Action[] list with the flat
 * (LwActionStream of LwActionLog records) declared in lw_action_stream.h.
 * For each Java ActionXxx class we expose ONE emit function whose
 * signature mirrors the Java constructor.  The function packs the
 * positional args into LwActionLog.v[] in the EXACT order Java's
 * getJSON() emits them (after the type id), so the Cython layer can
 * round-trip the byte-for-byte JSON shape upstream Python expects.
 *
 * Ownership / lifetime: the LwActions struct owns its LwActionStream by
 * value.  Caller passes a stable LwActions* through the engine, same as
 * Java's Actions.log() pattern.
 *
 * Not yet ported (string / dict payloads -- see lw_actions.c stubs):
 *   - ActionSay         (string message)
 *   - ActionShowCell    (hex color string)
 *   - ActionChestOpened (Map<int,int> resources)
 *   - Actions.toJSON / addOpsAndTimes (handled by the Cython binding)
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/action/
 */
#ifndef LW_ACTIONS_H
#define LW_ACTIONS_H

#include "lw_constants.h"
#include "lw_action_stream.h"

/* Forward decls -- Entity/Map/Cell/Attack/Weapon/Chip live in their own
 * headers; lw_actions.c reaches in for getFId() / getId() / getTemplate(). */
struct LwEntity;
struct LwAttack;
struct LwMap;
struct LwState;
struct LwWeapon;
struct LwChip;
typedef struct LwCell LwCell;


/* Java: public class Actions {
 *           private final List<Action> actions;
 *           private final List<Entity> entities = new ArrayList<>();
 *           private final ArrayNode leeks = ...;
 *           public final ObjectNode map = ...;
 *           public ObjectNode dead = ...;
 *           public ObjectNode ops = ...;
 *           public ObjectNode times = ...;
 *           private int mNextEffectId = 0;
 *       }
 *
 * NOTE: the JSON-tree fields (leeks, map, dead, ops, times) are emitted
 * during serialisation -- the Cython binding builds them from LwState.
 * Here we only carry the action stream + the next effect id.
 */
typedef struct LwActions {
    LwActionStream stream;   /* mirrors actions[] + mNextEffectId */
} LwActions;


/* public Actions() { this.actions = new ArrayList<>(); } */
void lw_actions_init(LwActions *self);

/* public Actions(Actions actions) { this.actions = new ArrayList<>(actions.actions); } */
void lw_actions_copy(LwActions *self, const LwActions *src);

/* public int getEffectId() { return mNextEffectId++; } */
int  lw_actions_get_effect_id(LwActions *self);

/* public void log(Action log) { actions.add(log); }
 *
 * Used internally by the emit functions below; exposed so callers that
 * synthesise their own LwActionLog (e.g. tests) can append directly. */
void lw_actions_log(LwActions *self, const LwActionLog *entry);

/* public int getNextId()  { return actions.size(); } */
int  lw_actions_get_next_id(const LwActions *self);

/* public int currentID() { return actions.size() - 1; } */
int  lw_actions_current_id(const LwActions *self);

/* public void addEntity(Entity entity, boolean critical) {
 *     ... builds an ObjectNode for the entity and appends to leeks[] ...
 * }
 *
 * NOTE: in C the leeks JSON is emitted by the Cython binding from
 * LwState; we only need to remember which entities were registered.
 * Stub for now -- the binding owns the JSON shape.
 */
void lw_actions_add_entity(LwActions *self, struct LwEntity *entity, int critical);

/* public void addMap(Map map) {
 *     ... builds the obstacles / type / width / height / pattern object ...
 * }
 *
 * NOTE: same as addEntity -- map JSON is built by the Cython binding
 * from LwMap. */
void lw_actions_add_map(LwActions *self, struct LwMap *map);


/* ----------------------------------------------------------------- */
/* One emit function per Java ActionXxx class.                        */
/* Field order in v[] MUST match Java getJSON() byte-for-byte after   */
/* the type id (the type id is stored in entry.type, never in v[]).   */
/* ----------------------------------------------------------------- */

/* ActionStartFight.getJSON() emits [START_FIGHT].  team1 / team2 are
 * stored on the action but never serialised; we still take them in the
 * signature to mirror the Java constructor. */
void lw_actions_log_start_fight(LwActions *self, int team1, int team2);

/* ActionNewTurn.getJSON() emits [NEW_TURN, count]. */
void lw_actions_log_new_turn(LwActions *self, int count);

/* ActionEntityTurn.getJSON() emits [LEEK_TURN, id].
 * NOTE: Java accepts a nullable Entity; pass NULL → id = -1. */
void lw_actions_log_entity_turn(LwActions *self, struct LwEntity *leek);

/* ActionEndTurn.getJSON() emits [END_TURN, target, pt, pm]. */
void lw_actions_log_end_turn(LwActions *self, struct LwEntity *target);

/* ActionMove.getJSON() emits [MOVE_TO, leek, end, [path...]].
 * The variable-length path is copied into stream.extra[] and referenced
 * via entry.extra_offset / extra_len.  The terminal cell of the path
 * (==end) is included in the path array, matching Java. */
void lw_actions_log_move(LwActions *self, struct LwEntity *leek,
                         const LwCell *const *path, int path_len);

/* ActionKill.getJSON() emits [KILL, caster, target].
 * NOTE: Java bug-for-bug -- the Java constructor sets
 *   this.caster = target.getFId();
 *   this.target = target.getFId();
 * so both v[] slots end up holding the target fid, regardless of
 * whatever caster pointer is passed in.  Preserved verbatim. */
void lw_actions_log_kill(LwActions *self, struct LwEntity *caster, struct LwEntity *target);

/* ActionUseWeapon.getJSON() emits [USE_WEAPON, cell, success]. */
void lw_actions_log_use_weapon(LwActions *self, const LwCell *cell, int success);

/* ActionUseChip.getJSON() emits [USE_CHIP, chip, cell, success].
 * Note Java emits chip BEFORE cell (template id then target cell). */
void lw_actions_log_use_chip(LwActions *self, const LwCell *cell,
                             struct LwChip *chip, int success);

/* ActionSetWeapon.getJSON() emits [SET_WEAPON, weapon].
 * NOTE: leek field exists in Java but is never set or emitted. */
void lw_actions_log_set_weapon(LwActions *self, struct LwWeapon *weapon);

/* ActionEntityDie.getJSON() emits [PLAYER_DEAD, id, killer?].
 * killer is appended only when killer != null (-1 in C) -- conditional
 * arg count.  n_args = 2 with killer, 1 without. */
void lw_actions_log_entity_die(LwActions *self, struct LwEntity *leek, struct LwEntity *killer);

/* ActionInvocation.getJSON() emits [SUMMON, owner, target, cell, result].
 * (Java: SUMMON action == 9.  Header comment marks it deprecated but
 * the Java engine still emits it via this class.) */
void lw_actions_log_invocation(LwActions *self, struct LwEntity *target, int result);

/* ActionResurrect.getJSON() emits [RESURRECT, owner, target, cell, life, max_life]. */
void lw_actions_log_resurrect(LwActions *self, struct LwEntity *owner, struct LwEntity *target);

/* ActionChestOpened.getJSON() emits [CHEST_OPENED, killer, chest, {resources}].
 * Resources is a Map<int,int> -- not representable in v[].
 * STUB: stores killer + chest fids in v[0..1] and skips the resources map.
 * The Cython binding currently fills the resources dict from elsewhere. */
void lw_actions_log_chest_opened(LwActions *self, struct LwEntity *killer, struct LwEntity *chest);

/* ActionStackEffect.getJSON() emits [STACK_EFFECT, id, value].
 * id refers to an effect_log_id previously returned by createEffect. */
void lw_actions_log_stack_effect(LwActions *self, int id, int value);

/* ActionUpdateEffect.getJSON() emits [UPDATE_EFFECT, id, value]. */
void lw_actions_log_update_effect(LwActions *self, int id, int value);

/* ActionRemoveEffect.getJSON() emits [REMOVE_EFFECT, id]. */
void lw_actions_log_remove_effect(LwActions *self, int id);

/* ActionReduceEffects.getJSON() emits [REDUCE_EFFECTS, id, value]. */
void lw_actions_log_reduce_effects(LwActions *self, struct LwEntity *target, int value);

/* ActionRemovePoisons.getJSON() emits [REMOVE_POISONS, id]. */
void lw_actions_log_remove_poisons(LwActions *self, struct LwEntity *target);

/* ActionRemoveShackles.getJSON() emits [REMOVE_SHACKLES, id]. */
void lw_actions_log_remove_shackles(LwActions *self, struct LwEntity *target);

/* ActionDamage.getJSON() emits [type.value, target, pv, erosion].
 * type.value is the Action.* code (LOST_LIFE / NOVA_DAMAGE / DAMAGE_RETURN
 * / LIFE_DAMAGE / POISON_DAMAGE -- see DamageType enum) and is stored
 * directly as entry.type. */
void lw_actions_log_damage(LwActions *self, int damage_type,
                           struct LwEntity *target, int pv, int erosion);

/* ActionHeal.getJSON() emits [HEAL, target, life]. */
void lw_actions_log_heal(LwActions *self, struct LwEntity *target, int life);

/* ActionVitality.getJSON() emits [VITALITY, target, life]. */
void lw_actions_log_vitality(LwActions *self, struct LwEntity *target, int life);

/* ActionNovaVitality.getJSON() emits [NOVA_VITALITY, target, life]. */
void lw_actions_log_nova_vitality(LwActions *self, struct LwEntity *target, int life);

/* ActionLama.getJSON() emits [LAMA].  No payload. */
void lw_actions_log_lama(LwActions *self);

/* ActionAIError.getJSON() emits [AI_ERROR, id]. */
void lw_actions_log_ai_error(LwActions *self, struct LwEntity *leek);

/* ActionSay.getJSON() emits [SAY, message] (string).
 * STUB: strings are not int-pack-able; entry just records SAY with no
 * payload until we extend the stream with a string pool. */
void lw_actions_log_say(LwActions *self, const char *message);

/* ActionShowCell.getJSON() emits [SHOW_CELL, cell, hexColor] (string).
 * STUB: stores cell only; the hex color string is omitted until we
 * extend the stream. */
void lw_actions_log_show_cell(LwActions *self, int cell, int color);


/* public static int createEffect(Actions logs, int type, int itemID,
 *                                Entity caster, Entity target, int effectID,
 *                                int value, int turns, int modifiers)
 *
 * From ActionAddEffect.createEffect():
 *   1. r = logs.getEffectId();
 *   2. log a new ActionAddEffect with id=r.
 *   3. return r.
 *
 * The constructor maps the input attack type to the Action code:
 *   - Attack.TYPE_CHIP   -> ADD_CHIP_EFFECT
 *   - Attack.TYPE_WEAPON -> ADD_WEAPON_EFFECT
 *   - anything else      -> passed through verbatim
 *
 * getJSON() emits [type, itemID, id, caster, target, effectID, value, turns, modifiers?].
 * modifiers is appended only when != 0 (n_args = 8 vs 7).
 *
 * Returns the effect_log_id assigned (used later by stack/update/remove). */
int  lw_actions_log_add_effect(LwActions *self, int attack_type, int item_id,
                               struct LwEntity *caster, struct LwEntity *target,
                               int effect_id, int value, int turns, int modifiers);


#endif /* LW_ACTIONS_H */
