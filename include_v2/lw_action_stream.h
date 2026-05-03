/*
 * lw_action_stream.h -- byte-flat action log, format aligned with Java.
 *
 * Each Java ActionXxx subclass has its own getJSON() that emits an
 * ArrayNode of variable length and mixed types. The closest C
 * equivalent is a flat (type, args[]) record where the number and
 * meaning of args is fixed per-type.
 *
 * Per-type field layouts (matches Java getJSON exactly):
 *
 *   START_FIGHT   (0)   : [team1_size, team2_size]
 *   END_FIGHT     (4)   : [winner_team]
 *   PLAYER_DEAD   (5)   : [target_fid]
 *   NEW_TURN      (6)   : [turn_number]
 *   LEEK_TURN     (7)   : [target_fid]                 -- ActionEntityTurn
 *   END_TURN      (8)   : [target_fid, total_tp, total_mp]
 *   SUMMON        (9)   : (deprecated -- INVOCATION used instead)
 *   MOVE_TO      (10)   : [leek_fid, end_cell, [path...]]
 *   KILL         (11)   : [caster_fid, target_fid]     -- caster==target, see ActionKill bug note
 *   USE_CHIP     (12)   : [caster_fid, chip_id, cell_id, success]
 *   SET_WEAPON   (13)   : [weapon_id]
 *   STACK_EFFECT (14)   : [effect_log_id, value]
 *   CHEST_OPENED (15)   : [chest_fid]
 *   USE_WEAPON   (16)   : [cell_id, success]
 *
 *   LOST_PT     (100)   : [target_fid, used_pt]
 *   LOST_LIFE   (101)   : [target_fid, pv, erosion]    -- ActionDamage(DIRECT)
 *   LOST_PM     (102)   : [target_fid, used_pm]
 *   HEAL        (103)   : [target_fid, value]
 *   VITALITY    (104)   : [target_fid, value]
 *   RESURRECT   (105)   : [target_fid, cell_id, hp]
 *   LOSE_STRENGTH(106)  : [target_fid, value]
 *   NOVA_DAMAGE (107)   : [target_fid, value]
 *   DAMAGE_RETURN(108)  : [target_fid, value, erosion]
 *   LIFE_DAMAGE (109)   : [target_fid, value]
 *   POISON_DAMAGE(110)  : [target_fid, value, erosion]
 *   AFTEREFFECT (111)   : [target_fid, value, erosion]
 *   NOVA_VITALITY(112)  : [target_fid, value]
 *
 *   ADD_WEAPON_EFFECT(301): [itemID, id, caster, target, effectID, value, turns, [modifiers]]
 *   ADD_CHIP_EFFECT  (302): [itemID, id, caster, target, effectID, value, turns, [modifiers]]
 *   REMOVE_EFFECT    (303): [effect_log_id]
 *   UPDATE_EFFECT    (304): [effect_log_id, new_value]
 *   REDUCE_EFFECTS   (306): [target_fid, percent]
 *   REMOVE_POISONS   (307): [target_fid]
 *   REMOVE_SHACKLES  (308): [target_fid]
 *
 *   ERROR     (1000)    : (no args)
 *   MAP       (1001)    : (variable, dropped from stream)
 *   AI_ERROR  (1002)    : (no args)
 *
 * Implementation:
 *   - Each entry is a tagged union with up to 8 ints.
 *   - Variable-length data (move paths) lives in a per-stream pool;
 *     the entry stores a (offset, length) pointer into that pool.
 *   - Entries are stored in capture order.
 *
 * The Cython binding can read entries back as Python dicts with the
 * exact JSON shape Java/Python upstream emits, enabling byte-exact
 * comparison.
 */
#ifndef LW_ACTION_STREAM_H
#define LW_ACTION_STREAM_H

#include "lw_constants.h"

#define LW_LOG_MAX_ENTRIES   2048
#define LW_LOG_MAX_EXTRA     8192   /* shared pool for paths etc. */
#define LW_LOG_MAX_ARGS      8


typedef struct {
    int  type;                          /* LW_ACTION_* */
    int  v[LW_LOG_MAX_ARGS];            /* positional payload (Java getJSON args after type) */
    int  n_args;                        /* number of valid v[] entries (0..8) */
    int  extra_offset;                  /* -1 if no payload, else index into stream.extra[] */
    int  extra_len;                     /* number of ints in extra[] for this entry */
} LwActionLog;


typedef struct {
    int          enabled;               /* 0 = silent (skip emit); 1 = log */
    int          n_entries;             /* number of entries written */
    LwActionLog  entries[LW_LOG_MAX_ENTRIES];

    /* Pool for variable-length payload (only ActionMove uses this so far). */
    int          extra[LW_LOG_MAX_EXTRA];
    int          n_extra;

    /* Sequential id assigned to each ActionAddEffect entry (Actions.getEffectId
     * in Java).  REMOVE_EFFECT / UPDATE_EFFECT / STACK_EFFECT reference it. */
    int          next_effect_log_id;
} LwActionStream;


#endif /* LW_ACTION_STREAM_H */
