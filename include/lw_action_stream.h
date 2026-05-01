/*
 * lw_action_stream.h -- byte-flat action log emitted during a fight.
 *
 * Mirrors the subset of Python's ``state.actions`` that the C
 * engine actually emits. Each entry is a small fixed-size struct:
 * (type, caster_id, target_id, v1, v2, v3). Replays / Java-parity
 * gates can read this back via the Cython binding and diff against
 * the upstream Python's action stream.
 *
 * Capacity is fixed (LW_LOG_MAX_ENTRIES) so the stream lives inside
 * LwState and travels with state clones at memcpy speed. Emitting
 * past capacity is a silent no-op; callers should size their fights
 * appropriately or drop the stream when running AI search inner loops.
 */
#ifndef LW_ACTION_STREAM_H
#define LW_ACTION_STREAM_H

#include "lw_types.h"

#define LW_LOG_MAX_ENTRIES  512

/* Action type ids. Lined up with Python's
 * leekwars/action/action.py::Action.* constants where they exist;
 * leave gaps for future entries. */
typedef enum {
    LW_ACT_NONE              = 0,
    LW_ACT_USE_WEAPON        = 1,
    LW_ACT_USE_CHIP          = 2,
    LW_ACT_DAMAGE            = 3,
    LW_ACT_HEAL              = 4,
    LW_ACT_ADD_EFFECT        = 5,
    LW_ACT_REMOVE_EFFECT     = 6,
    LW_ACT_STACK_EFFECT      = 7,
    LW_ACT_KILL              = 8,
    LW_ACT_MOVE              = 9,
    LW_ACT_SLIDE             = 10,
    LW_ACT_TELEPORT          = 11,
    LW_ACT_END_TURN          = 12,
    LW_ACT_START_TURN        = 13,
    LW_ACT_INVOCATION        = 14,
    LW_ACT_RESURRECT         = 15,
    LW_ACT_VITALITY          = 16,
    LW_ACT_NOVA_VITALITY     = 17,
    LW_ACT_REDUCE_EFFECTS    = 18,
    LW_ACT_REMOVE_POISONS    = 19,
    LW_ACT_REMOVE_SHACKLES   = 20,
    LW_ACT_ADD_STATE         = 21,
    LW_ACT_CRITICAL          = 22,
    LW_ACT_USE_INVALID       = 23,
} LwActionLogType;


typedef struct {
    int type;        /* LwActionLogType */
    int caster_id;   /* -1 if N/A */
    int target_id;   /* -1 if N/A */
    int value1;      /* primary numeric (damage, heal, value...) */
    int value2;      /* secondary (erosion, turns, effect_id...) */
    int value3;      /* tertiary (item_id, modifiers...) */
} LwActionLog;


typedef struct {
    int          enabled;      /* 0 = silent (skip emit); 1 = log */
    int          n;            /* number of entries */
    LwActionLog  entries[LW_LOG_MAX_ENTRIES];
} LwActionStream;

#endif /* LW_ACTION_STREAM_H */
