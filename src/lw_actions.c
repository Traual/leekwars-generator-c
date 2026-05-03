/*
 * lw_actions.c -- 1:1 port of action/Actions.java + each action/ActionXxx.java
 *
 * Each lw_actions_log_* function builds a LwActionLog whose v[] slot
 * order matches the Java getJSON() output (skipping the first element,
 * which is the type id and is stored in entry.type instead of v[]).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/action/
 */

#include <stddef.h>

#include "lw_actions.h"
#include "lw_cell.h"


/* ----------------------------------------------------------------- */
/* Forward extern declarations for the few accessors we call.         */
/* The owning headers (lw_entity.h, lw_map.h, lw_weapon.h, lw_chip.h) */
/* are not yet ported -- declaring the prototypes here keeps          */
/* lw_actions.c self-contained and trivially relocatable later.       */
/* ----------------------------------------------------------------- */

struct LwEntity;
struct LwMap;
struct LwWeapon;
struct LwChip;

/* All these getters live in lw_entity.h (mostly static inline). */
#include "lw_entity.h"
extern int  lw_entity_get_summoner_fid(const struct LwEntity *self);  /* entity.getSummoner().getFId() in lw_glue.c */

/* lw_weapon_get_template / lw_chip_get_template are static inline in
 * lw_weapon.h / lw_chip.h. Including those headers lets the compiler
 * inline directly. */
#include "lw_weapon.h"
#include "lw_chip.h"


/* ----------------------------------------------------------------- */
/* Internal helpers                                                   */
/* ----------------------------------------------------------------- */

/* Reset a log entry to "no payload" defaults, then commit it.
 * Mirrors Java's `actions.add(new ActionXxx(...))`. */
static void log_entry_init(LwActionLog *e, int type) {
    e->type = type;
    e->n_args = 0;
    e->extra_offset = -1;
    e->extra_len = 0;
    for (int i = 0; i < LW_LOG_MAX_ARGS; i++) e->v[i] = 0;
}

/* Append the assembled entry to the stream (mirrors `logs.log(this)`). */
static void log_commit(LwActions *self, const LwActionLog *e) {
    lw_actions_log(self, e);
}


/* ================================================================= */
/* Actions.java                                                       */
/* ================================================================= */

/* public Actions() { this.actions = new ArrayList<Action>(); } */
void lw_actions_init(LwActions *self) {
    self->stream.enabled = 1;
    self->stream.n_entries = 0;
    self->stream.n_extra = 0;
    self->stream.next_effect_log_id = 0;
}

/* public Actions(Actions actions) { this.actions = new ArrayList<>(actions.actions); } */
void lw_actions_copy(LwActions *self, const LwActions *src) {
    self->stream.enabled = src->stream.enabled;
    self->stream.n_entries = src->stream.n_entries;
    self->stream.n_extra = src->stream.n_extra;
    self->stream.next_effect_log_id = src->stream.next_effect_log_id;
    for (int i = 0; i < src->stream.n_entries; i++) {
        self->stream.entries[i] = src->stream.entries[i];
    }
    for (int i = 0; i < src->stream.n_extra; i++) {
        self->stream.extra[i] = src->stream.extra[i];
    }
}

/* public int getEffectId() { return mNextEffectId++; } */
int lw_actions_get_effect_id(LwActions *self) {
    return self->stream.next_effect_log_id++;
}

/* public void log(Action log) { actions.add(log); } */
void lw_actions_log(LwActions *self, const LwActionLog *entry) {
    if (!self->stream.enabled) return;
    if (self->stream.n_entries >= LW_LOG_MAX_ENTRIES) return;  /* drop on overflow */
    self->stream.entries[self->stream.n_entries++] = *entry;
}

/* public int getNextId() { return actions.size(); } */
int lw_actions_get_next_id(const LwActions *self) {
    return self->stream.n_entries;
}

/* public int currentID() { return actions.size() - 1; } */
int lw_actions_current_id(const LwActions *self) {
    return self->stream.n_entries - 1;
}

/* public void addEntity(Entity entity, boolean critical) {
 *     entities.add(entity);
 *     // builds an ObjectNode with id/level/skin/hat/metal/face/life/...
 *     // and appends it to leeks[].
 * }
 *
 * NOTE: in the C engine this metadata lives on LwEntity directly; the
 * Cython binding walks LwState->entities to build the JSON.  Stubbed.
 */
void lw_actions_add_entity(LwActions *self, struct LwEntity *entity, int critical) {
    (void)self; (void)entity; (void)critical;
    /* No-op -- JSON projection happens at the Cython binding layer. */
}

/* public void addMap(Map map) {
 *     // builds {id?, obstacles, type, width, height, pattern?}.
 * }
 *
 * NOTE: same as addEntity -- the binding owns the JSON shape from LwMap.
 */
void lw_actions_add_map(LwActions *self, struct LwMap *map) {
    (void)self; (void)map;
    /* No-op -- JSON projection happens at the Cython binding layer. */
}


/* ================================================================= */
/* ActionStartFight.java                                              */
/* ================================================================= */

/* public ActionStartFight(int team1, int team2) { ... }
 * getJSON -> [START_FIGHT]   (team1/team2 are stored but never serialised)
 */
void lw_actions_log_start_fight(LwActions *self, int team1, int team2) {
    (void)team1; (void)team2;   /* stored on the Java object but not in getJSON */
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_START_FIGHT);
    /* n_args = 0 -- matches Java getJSON which only adds the type id. */
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionNewTurn.java                                                 */
/* ================================================================= */

/* public ActionNewTurn(int count) { this.count = count; }
 * getJSON -> [NEW_TURN, count]
 */
void lw_actions_log_new_turn(LwActions *self, int count) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_NEW_TURN);
    e.v[0] = count;
    e.n_args = 1;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionEntityTurn.java                                              */
/* ================================================================= */

/* public ActionEntityTurn(Entity leek) {
 *     if (leek == null) this.id = -1;
 *     else this.id = leek.getFId();
 * }
 * getJSON -> [LEEK_TURN, id]
 */
void lw_actions_log_entity_turn(LwActions *self, struct LwEntity *leek) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_LEEK_TURN);
    e.v[0] = (leek == NULL) ? -1 : lw_entity_get_fid(leek);
    e.n_args = 1;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionEndTurn.java                                                 */
/* ================================================================= */

/* public ActionEndTurn(Entity target) {
 *     this.target = target.getFId();
 *     this.pt = target.getTP();
 *     this.pm = target.getMP();
 * }
 * getJSON -> [END_TURN, target, pt, pm]
 */
void lw_actions_log_end_turn(LwActions *self, struct LwEntity *target) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_END_TURN);
    e.v[0] = lw_entity_get_fid(target);
    e.v[1] = lw_entity_get_tp(target);
    e.v[2] = lw_entity_get_mp(target);
    e.n_args = 3;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionMove.java                                                    */
/* ================================================================= */

/* public ActionMove(Entity leek, List<Cell> path) {
 *     this.leek = leek.getFId();
 *     this.path = new int[path.size()];
 *     for (int i = 0; i < path.size(); i++)
 *         this.path[i] = path.get(i).getId();
 *     end = path.get(path.size() - 1).getId();
 * }
 * getJSON -> [MOVE_TO, leek, end, [path...]]
 */
void lw_actions_log_move(LwActions *self, struct LwEntity *leek,
                         const LwCell *const *path, int path_len) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_MOVE_TO);
    e.v[0] = lw_entity_get_fid(leek);
    e.v[1] = (path_len > 0) ? path[path_len - 1]->id : 0;  /* path.get(size-1).getId() */
    e.n_args = 2;

    /* Copy the path into the per-stream extra pool. */
    if (path_len > 0 &&
        self->stream.n_extra + path_len <= LW_LOG_MAX_EXTRA) {
        e.extra_offset = self->stream.n_extra;
        e.extra_len = path_len;
        for (int i = 0; i < path_len; i++) {
            self->stream.extra[self->stream.n_extra++] = path[i]->id;
        }
    }
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionKill.java                                                    */
/* ================================================================= */

/* public ActionKill(Entity caster, Entity target) {
 *     this.caster = target.getFId();   // <-- Java bug: uses target, not caster
 *     this.target = target.getFId();
 * }
 * getJSON -> [KILL, caster, target]
 *
 * Bug-for-bug port: caster slot ends up holding target.getFId() too.
 */
void lw_actions_log_kill(LwActions *self, struct LwEntity *caster, struct LwEntity *target) {
    (void)caster;  /* Java ignores caster -- preserve verbatim. */
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_KILL);
    e.v[0] = lw_entity_get_fid(target);   /* "caster" slot */
    e.v[1] = lw_entity_get_fid(target);   /* "target" slot */
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionUseWeapon.java                                               */
/* ================================================================= */

/* public ActionUseWeapon(Cell cell, int success) {
 *     this.cell = cell.getId();
 *     this.success = success;
 * }
 * getJSON -> [USE_WEAPON, cell, success]
 */
void lw_actions_log_use_weapon(LwActions *self, const LwCell *cell, int success) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_USE_WEAPON);
    e.v[0] = cell->id;
    e.v[1] = success;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionUseChip.java                                                 */
/* ================================================================= */

/* public ActionUseChip(Cell cell, Chip chip, int success) {
 *     this.cell = cell.getId();
 *     this.chip = chip.getTemplate();
 *     this.success = success;
 * }
 * getJSON -> [USE_CHIP, chip, cell, success]   (note: chip THEN cell)
 */
void lw_actions_log_use_chip(LwActions *self, const LwCell *cell,
                             struct LwChip *chip, int success) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_USE_CHIP);
    e.v[0] = lw_chip_get_template(chip);
    e.v[1] = cell->id;
    e.v[2] = success;
    e.n_args = 3;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionSetWeapon.java                                               */
/* ================================================================= */

/* public ActionSetWeapon(Weapon weapon) { this.weapon = weapon.getTemplate(); }
 * getJSON -> [SET_WEAPON, weapon]
 *
 * NOTE: the Java class also has a `leek` field but it is never assigned
 * and never serialised.  Mirrored: we don't take a leek argument.
 */
void lw_actions_log_set_weapon(LwActions *self, struct LwWeapon *weapon) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_SET_WEAPON);
    e.v[0] = lw_weapon_get_template(weapon);
    e.n_args = 1;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionEntityDie.java                                               */
/* ================================================================= */

/* public ActionEntityDie(Entity leek, Entity killer) {
 *     this.id = leek.getFId();
 *     this.killer = killer != null ? killer.getFId() : -1;
 * }
 * getJSON -> [PLAYER_DEAD, id, killer?]
 *   killer appended only if killer != -1.
 */
void lw_actions_log_entity_die(LwActions *self, struct LwEntity *leek, struct LwEntity *killer) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_PLAYER_DEAD);
    e.v[0] = lw_entity_get_fid(leek);
    if (killer != NULL) {
        e.v[1] = lw_entity_get_fid(killer);
        e.n_args = 2;
    } else {
        e.n_args = 1;
    }
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionInvocation.java                                              */
/* ================================================================= */

/* public ActionInvocation(Entity target, int result) {
 *     this.owner = target.getSummoner().getFId();
 *     this.target = target.getFId();
 *     this.cell = target.getCell().getId();
 *     this.result = result;
 * }
 * getJSON -> [SUMMON, owner, target, cell, result]
 */
void lw_actions_log_invocation(LwActions *self, struct LwEntity *target, int result) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_SUMMON);
    e.v[0] = lw_entity_get_summoner_fid(target);
    e.v[1] = lw_entity_get_fid(target);
    e.v[2] = lw_entity_get_cell_id(target);
    e.v[3] = result;
    e.n_args = 4;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionResurrect.java                                               */
/* ================================================================= */

/* public ActionResurrect(Entity owner, Entity target) {
 *     this.owner = owner.getFId();
 *     this.target = target.getFId();
 *     this.cell = target.getCell().getId();
 *     this.life = target.getLife();
 *     this.max_life = target.getTotalLife();
 * }
 * getJSON -> [RESURRECT, owner, target, cell, life, max_life]
 */
void lw_actions_log_resurrect(LwActions *self, struct LwEntity *owner, struct LwEntity *target) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_RESURRECT);
    e.v[0] = lw_entity_get_fid(owner);
    e.v[1] = lw_entity_get_fid(target);
    e.v[2] = lw_entity_get_cell_id(target);
    e.v[3] = lw_entity_get_life(target);
    e.v[4] = lw_entity_get_total_life(target);
    e.n_args = 5;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionChestOpened.java                                             */
/* ================================================================= */

/* public ActionChestOpened(Entity killer, Entity chest, Map<Integer,Integer> resources) { ... }
 * getJSON -> [CHEST_OPENED, killer, chest, {resources...}]
 *
 * STUB: the resources map is not int-pack-able; we record killer + chest
 * fids only.  The Cython binding fills the resources dict separately.
 */
void lw_actions_log_chest_opened(LwActions *self, struct LwEntity *killer, struct LwEntity *chest) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_CHEST_OPENED);
    e.v[0] = lw_entity_get_fid(killer);
    e.v[1] = lw_entity_get_fid(chest);
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionStackEffect.java                                             */
/* ================================================================= */

/* public ActionStackEffect(int id, int value) { this.id = id; this.value = value; }
 * getJSON -> [STACK_EFFECT, id, value]
 */
void lw_actions_log_stack_effect(LwActions *self, int id, int value) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_STACK_EFFECT);
    e.v[0] = id;
    e.v[1] = value;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionUpdateEffect.java                                            */
/* ================================================================= */

/* public ActionUpdateEffect(int id, int value) { ... }
 * getJSON -> [UPDATE_EFFECT, id, value]
 */
void lw_actions_log_update_effect(LwActions *self, int id, int value) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_UPDATE_EFFECT);
    e.v[0] = id;
    e.v[1] = value;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionRemoveEffect.java                                            */
/* ================================================================= */

/* public ActionRemoveEffect(int id) { this.id = id; }
 * getJSON -> [REMOVE_EFFECT, id]
 */
void lw_actions_log_remove_effect(LwActions *self, int id) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_REMOVE_EFFECT);
    e.v[0] = id;
    e.n_args = 1;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionReduceEffects.java                                           */
/* ================================================================= */

/* public ActionReduceEffects(Entity target, int value) {
 *     this.id = target.getFId();
 *     this.value = value;
 * }
 * getJSON -> [REDUCE_EFFECTS, id, value]
 */
void lw_actions_log_reduce_effects(LwActions *self, struct LwEntity *target, int value) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_REDUCE_EFFECTS);
    e.v[0] = lw_entity_get_fid(target);
    e.v[1] = value;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionRemovePoisons.java                                           */
/* ================================================================= */

/* public ActionRemovePoisons(Entity target) { this.id = target.getFId(); }
 * getJSON -> [REMOVE_POISONS, id]
 */
void lw_actions_log_remove_poisons(LwActions *self, struct LwEntity *target) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_REMOVE_POISONS);
    e.v[0] = lw_entity_get_fid(target);
    e.n_args = 1;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionRemoveShackles.java                                          */
/* ================================================================= */

/* public ActionRemoveShackles(Entity target) { this.id = target.getFId(); }
 * getJSON -> [REMOVE_SHACKLES, id]
 */
void lw_actions_log_remove_shackles(LwActions *self, struct LwEntity *target) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_REMOVE_SHACKLES);
    e.v[0] = lw_entity_get_fid(target);
    e.n_args = 1;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionDamage.java                                                  */
/* ================================================================= */

/* public ActionDamage(DamageType type, Entity target, int pv, int erosion) {
 *     this.type = type;       // DamageType enum carrying the action code
 *     this.target = target.getFId();
 *     this.pv = pv;
 *     this.erosion = erosion;
 * }
 * getJSON -> [type.value, target, pv, erosion]
 *
 * NOTE: type.value is the action code (LOST_LIFE / NOVA_DAMAGE / RETURN
 * / LIFE_DAMAGE / POISON_DAMAGE / AFTEREFFECT).  In Java the enum stores
 * AFTEREFFECT as 110 (same numeric as POISON) -- bug-for-bug, the
 * caller decides which code is passed in.
 */
void lw_actions_log_damage(LwActions *self, int damage_type,
                           struct LwEntity *target, int pv, int erosion) {
    LwActionLog e;
    log_entry_init(&e, damage_type);
    e.v[0] = lw_entity_get_fid(target);
    e.v[1] = pv;
    e.v[2] = erosion;
    e.n_args = 3;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionHeal.java                                                    */
/* ================================================================= */

/* public ActionHeal(Entity target, int life) { ... }
 * getJSON -> [HEAL, target, life]
 */
void lw_actions_log_heal(LwActions *self, struct LwEntity *target, int life) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_HEAL);
    e.v[0] = lw_entity_get_fid(target);
    e.v[1] = life;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionVitality.java                                                */
/* ================================================================= */

/* public ActionVitality(Entity target, int life) { ... }
 * getJSON -> [VITALITY, target, life]
 */
void lw_actions_log_vitality(LwActions *self, struct LwEntity *target, int life) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_VITALITY);
    e.v[0] = lw_entity_get_fid(target);
    e.v[1] = life;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionNovaVitality.java                                            */
/* ================================================================= */

/* public ActionNovaVitality(Entity target, int life) { ... }
 * getJSON -> [NOVA_VITALITY, target, life]
 */
void lw_actions_log_nova_vitality(LwActions *self, struct LwEntity *target, int life) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_NOVA_VITALITY);
    e.v[0] = lw_entity_get_fid(target);
    e.v[1] = life;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionLama.java                                                    */
/* ================================================================= */

/* public ActionLama() {}
 * getJSON -> [LAMA]
 */
void lw_actions_log_lama(LwActions *self) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_LAMA);
    /* n_args = 0 */
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionAIError.java                                                 */
/* ================================================================= */

/* public ActionAIError(Entity leek) {
 *     if (leek == null) this.id = -1;
 *     else this.id = leek.getFId();
 * }
 * getJSON -> [AI_ERROR, id]
 */
void lw_actions_log_ai_error(LwActions *self, struct LwEntity *leek) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_AI_ERROR);
    e.v[0] = (leek == NULL) ? -1 : lw_entity_get_fid(leek);
    e.n_args = 1;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionSay.java                                                     */
/* ================================================================= */

/* public ActionSay(String message) { this.message = message; }
 * getJSON -> [SAY, message.replaceAll("\t", "    ")]
 *
 * STUB: strings are not int-pack-able into v[].  The entry records SAY
 * with no payload -- the Cython binding can supplement the message via
 * a side channel until we extend LwActionStream with a string pool.
 */
void lw_actions_log_say(LwActions *self, const char *message) {
    (void)message;
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_SAY);
    /* n_args = 0 -- string payload omitted */
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionShowCell.java                                                */
/* ================================================================= */

/* public ActionShowCell(int cell, int color) { mCell = cell; mColor = color; }
 * getJSON -> [SHOW_CELL, mCell, Util.getHexaColor(mColor)]   (color is a string)
 *
 * STUB: stores the cell as v[0] and the raw int color as v[1]; the
 * Cython binding can stringify mColor via Util.getHexaColor when
 * emitting the final JSON.
 */
void lw_actions_log_show_cell(LwActions *self, int cell, int color) {
    LwActionLog e;
    log_entry_init(&e, LW_ACTION_SHOW_CELL);
    e.v[0] = cell;
    e.v[1] = color;
    e.n_args = 2;
    log_commit(self, &e);
}


/* ================================================================= */
/* ActionAddEffect.java                                               */
/* ================================================================= */

/* public static int createEffect(Actions logs, int type, int itemID,
 *                                Entity caster, Entity target,
 *                                int effectID, int value, int turns,
 *                                int modifiers) {
 *     int r = logs.getEffectId();
 *     ActionAddEffect effect = new ActionAddEffect(type, itemID, r,
 *                                                  caster.getFId(),
 *                                                  target.getFId(),
 *                                                  effectID, value, turns, modifiers);
 *     logs.log(effect);
 *     return r;
 * }
 *
 * Constructor maps attack type -> action code:
 *   if (type == Attack.TYPE_CHIP)   this.type = Action.ADD_CHIP_EFFECT;
 *   else if (type == Attack.TYPE_WEAPON) this.type = Action.ADD_WEAPON_EFFECT;
 *   else this.type = type;
 *
 * getJSON -> [type, itemID, id, caster, target, effectID, value, turns, modifiers?]
 *   modifiers appended only when != 0.
 */
int lw_actions_log_add_effect(LwActions *self, int attack_type, int item_id,
                              struct LwEntity *caster, struct LwEntity *target,
                              int effect_id, int value, int turns, int modifiers) {

    int r = lw_actions_get_effect_id(self);

    /* Map Attack.TYPE_* to the corresponding ADD_*_EFFECT action code. */
    int action_type;
    if (attack_type == LW_ATTACK_TYPE_CHIP) {
        action_type = LW_ACTION_ADD_CHIP_EFFECT;
    } else if (attack_type == LW_ATTACK_TYPE_WEAPON) {
        action_type = LW_ACTION_ADD_WEAPON_EFFECT;
    } else {
        action_type = attack_type;
    }

    LwActionLog e;
    log_entry_init(&e, action_type);
    e.v[0] = item_id;
    e.v[1] = r;
    e.v[2] = lw_entity_get_fid(caster);
    e.v[3] = lw_entity_get_fid(target);
    e.v[4] = effect_id;
    e.v[5] = value;
    e.v[6] = turns;
    if (modifiers != 0) {
        e.v[7] = modifiers;
        e.n_args = 8;
    } else {
        e.n_args = 7;
    }
    log_commit(self, &e);

    return r;
}
