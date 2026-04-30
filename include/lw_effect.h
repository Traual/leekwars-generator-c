/*
 * lw_effect.h -- Effect struct (active modifier on an entity).
 *
 * Effects model damage-over-time, buffs, shields, etc. They tick
 * once per turn and self-destruct when their counter expires.
 *
 * Layout matches Python's Effect class:
 *   - id (TYPE_*)
 *   - turns remaining
 *   - value/value1/value2 (numeric strength)
 *   - aoe (area-of-effect multiplier)
 *   - critical / criticalPower (damage scaling)
 *   - jet (chip jet param)
 *   - erosion_rate (erosion)
 *   - target_id, caster_id (entity ids; -1 if dead/missing)
 *   - attack_id (chip/weapon id; -1 if buff applied directly)
 *   - log_id (link to action log; 0 if not logged)
 *   - stats[18] (per-stat modifiers for buffs)
 */
#ifndef LW_EFFECT_H
#define LW_EFFECT_H

#include "lw_types.h"

typedef struct {
    int     id;                 /* TYPE_* (1..62) */
    int     turns;
    double  value1, value2;
    double  aoe;
    int     critical;           /* boolean */
    double  critical_power;
    double  jet;
    double  erosion_rate;
    int     value;              /* the "headline" number (damage, heal, buff amt) */
    int     previous_total;
    int     target_count;
    int     propagate;
    int     modifiers;
    int     log_id;

    int     target_id;          /* -1 if absent */
    int     caster_id;          /* -1 if absent */
    int     attack_id;          /* -1 if no attack (raw buff) */

    int     stats[LW_STAT_COUNT];
} LwEffect;

/* Effect type IDs (full catalog -- mirrors leekwars/effect/effect.py).
 * The numeric values are part of the Java <-> Python <-> C wire format
 * (they're stored in attack definitions, action log entries, etc.) so
 * they MUST match Python exactly. */
#define LW_EFFECT_DAMAGE                     1
#define LW_EFFECT_HEAL                       2
#define LW_EFFECT_BUFF_STRENGTH              3
#define LW_EFFECT_BUFF_AGILITY               4
#define LW_EFFECT_RELATIVE_SHIELD            5
#define LW_EFFECT_ABSOLUTE_SHIELD            6
#define LW_EFFECT_BUFF_MP                    7
#define LW_EFFECT_BUFF_TP                    8
#define LW_EFFECT_DEBUFF                     9
#define LW_EFFECT_TELEPORT                  10
#define LW_EFFECT_PERMUTATION               11
#define LW_EFFECT_VITALITY                  12
#define LW_EFFECT_POISON                    13
#define LW_EFFECT_SUMMON                    14
#define LW_EFFECT_RESURRECT                 15
#define LW_EFFECT_KILL                      16
#define LW_EFFECT_SHACKLE_MP                17
#define LW_EFFECT_SHACKLE_TP                18
#define LW_EFFECT_SHACKLE_STRENGTH          19
#define LW_EFFECT_DAMAGE_RETURN             20
#define LW_EFFECT_BUFF_RESISTANCE           21
#define LW_EFFECT_BUFF_WISDOM               22
#define LW_EFFECT_ANTIDOTE                  23
#define LW_EFFECT_SHACKLE_MAGIC             24
#define LW_EFFECT_AFTEREFFECT               25
#define LW_EFFECT_VULNERABILITY             26
#define LW_EFFECT_ABSOLUTE_VULNERABILITY    27
#define LW_EFFECT_LIFE_DAMAGE               28
#define LW_EFFECT_STEAL_ABSOLUTE_SHIELD     29
#define LW_EFFECT_NOVA_DAMAGE               30
#define LW_EFFECT_RAW_BUFF_MP               31
#define LW_EFFECT_RAW_BUFF_TP               32
#define LW_EFFECT_POISON_TO_SCIENCE         33
#define LW_EFFECT_DAMAGE_TO_ABSOLUTE_SHIELD 34
#define LW_EFFECT_DAMAGE_TO_STRENGTH        35
#define LW_EFFECT_NOVA_DAMAGE_TO_MAGIC      36
#define LW_EFFECT_RAW_ABSOLUTE_SHIELD       37
#define LW_EFFECT_RAW_BUFF_STRENGTH         38
#define LW_EFFECT_RAW_BUFF_MAGIC            39
#define LW_EFFECT_RAW_BUFF_SCIENCE          40
#define LW_EFFECT_RAW_BUFF_AGILITY          41
#define LW_EFFECT_RAW_BUFF_RESISTANCE       42
#define LW_EFFECT_PROPAGATION               43
#define LW_EFFECT_RAW_BUFF_WISDOM           44
#define LW_EFFECT_NOVA_VITALITY             45
#define LW_EFFECT_ATTRACT                   46
#define LW_EFFECT_SHACKLE_AGILITY           47
#define LW_EFFECT_SHACKLE_WISDOM            48
#define LW_EFFECT_REMOVE_SHACKLES           49
#define LW_EFFECT_MOVED_TO_MP               50
#define LW_EFFECT_PUSH                      51
#define LW_EFFECT_RAW_BUFF_POWER            52
#define LW_EFFECT_REPEL                     53
#define LW_EFFECT_RAW_RELATIVE_SHIELD       54
#define LW_EFFECT_ALLY_KILLED_TO_AGILITY    55
#define LW_EFFECT_KILL_TO_TP                56
#define LW_EFFECT_RAW_HEAL                  57
#define LW_EFFECT_CRITICAL_TO_HEAL          58
#define LW_EFFECT_ADD_STATE                 59
#define LW_EFFECT_TOTAL_DEBUFF              60
#define LW_EFFECT_STEAL_LIFE                61
#define LW_EFFECT_MULTIPLY_STATS            62

/* Target filter bits (matches Effect.TARGET_*). */
#define LW_TARGET_ENEMIES       1
#define LW_TARGET_ALLIES        2
#define LW_TARGET_CASTER        4
#define LW_TARGET_NON_SUMMONS   8
#define LW_TARGET_SUMMONS      16

/* Modifier bits (matches Effect.MODIFIER_*). */
#define LW_MODIFIER_STACKABLE              1
#define LW_MODIFIER_MULTIPLIED_BY_TARGETS  2
#define LW_MODIFIER_ON_CASTER              4
#define LW_MODIFIER_NOT_REPLACEABLE        8
#define LW_MODIFIER_IRREDUCTIBLE          16

/* Erosion rates (Effect.EROSION_DAMAGE / EROSION_POISON). */
#define LW_EROSION_DAMAGE   0.05
#define LW_EROSION_POISON   0.10

#endif /* LW_EFFECT_H */
