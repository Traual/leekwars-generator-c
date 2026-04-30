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

/* Effect type IDs (subset we encode explicitly; the engine uses many more) */
#define LW_EFFECT_DAMAGE              1
#define LW_EFFECT_HEAL                2
#define LW_EFFECT_BUFF_STRENGTH       3
#define LW_EFFECT_BUFF_AGILITY        4
#define LW_EFFECT_RELATIVE_SHIELD     5
#define LW_EFFECT_ABSOLUTE_SHIELD     6
#define LW_EFFECT_BUFF_MP             7
#define LW_EFFECT_TELEPORT           10
#define LW_EFFECT_POISON             13
#define LW_EFFECT_SUMMON             14
#define LW_EFFECT_SHACKLE_MP         17
#define LW_EFFECT_SHACKLE_TP         18
#define LW_EFFECT_SHACKLE_STRENGTH   19
#define LW_EFFECT_DAMAGE_RETURN      20
#define LW_EFFECT_SHACKLE_MAGIC      24
#define LW_EFFECT_SHACKLE_AGILITY    47
#define LW_EFFECT_SHACKLE_WISDOM     48

#endif /* LW_EFFECT_H */
