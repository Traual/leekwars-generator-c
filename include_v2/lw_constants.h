/*
 * lw_constants.h -- 1:1 port of Java constants used across the engine.
 *
 * Source files (java_reference/src/main/java/com/leekwars/generator/):
 *   - action/Action.java        : ACTION_* (0..1002)
 *   - attack/Attack.java        : ATTACK_USE_*, ATTACK_LAUNCH_*, ATTACK_TYPE_*
 *   - attack/EntityState.java   : ENTITY_STATE_*
 *   - effect/Effect.java        : EFFECT_TYPE_*, TARGET_*, MODIFIER_*, CRITICAL_FACTOR, EROSION_*
 *   - state/Entity.java         : STAT_*, TYPE_LEEK..MOB, SAY_LIMIT_TURN, SHOW_LIMIT_TURN
 *   - state/State.java          : STATE_*, TYPE_SOLO..COLOSSUS, MAX_TURNS, SUMMON_LIMIT
 *
 * RULES:
 *   - Names match Java symbol names exactly except for the C-style
 *     prefix (LW_) which avoids collisions with libc/system headers.
 *   - Values match Java exactly (verified by grepping the source).
 *   - Comments include the original Java line for traceability.
 */
#ifndef LW_CONSTANTS_H
#define LW_CONSTANTS_H

/* ---- action/Action.java ----------------------------------------- */

/* Actions */
#define LW_ACTION_START_FIGHT         0
#define LW_ACTION_END_FIGHT           4
#define LW_ACTION_PLAYER_DEAD         5
#define LW_ACTION_NEW_TURN            6
#define LW_ACTION_LEEK_TURN           7   /* aka START_TURN in stream */
#define LW_ACTION_END_TURN            8
#define LW_ACTION_SUMMON              9
#define LW_ACTION_MOVE_TO            10
#define LW_ACTION_KILL               11
#define LW_ACTION_USE_CHIP           12
#define LW_ACTION_SET_WEAPON         13
#define LW_ACTION_STACK_EFFECT       14
#define LW_ACTION_CHEST_OPENED       15
#define LW_ACTION_USE_WEAPON         16

/* Buffs */
#define LW_ACTION_LOST_PT           100
#define LW_ACTION_LOST_LIFE         101
#define LW_ACTION_LOST_PM           102
#define LW_ACTION_HEAL              103
#define LW_ACTION_VITALITY          104
#define LW_ACTION_RESURRECT         105
#define LW_ACTION_LOSE_STRENGTH     106
#define LW_ACTION_NOVA_DAMAGE       107
#define LW_ACTION_DAMAGE_RETURN     108
#define LW_ACTION_LIFE_DAMAGE       109
#define LW_ACTION_POISON_DAMAGE     110
#define LW_ACTION_AFTEREFFECT       111
#define LW_ACTION_NOVA_VITALITY     112

/* "fun" */
#define LW_ACTION_LAMA              201
#define LW_ACTION_SAY               203
#define LW_ACTION_SHOW_CELL         205

/* Effects */
#define LW_ACTION_ADD_WEAPON_EFFECT 301
#define LW_ACTION_ADD_CHIP_EFFECT   302
#define LW_ACTION_REMOVE_EFFECT     303
#define LW_ACTION_UPDATE_EFFECT     304
#define LW_ACTION_REDUCE_EFFECTS    306
#define LW_ACTION_REMOVE_POISONS    307
#define LW_ACTION_REMOVE_SHACKLES   308

/* Other */
#define LW_ACTION_ERROR            1000
#define LW_ACTION_MAP              1001
#define LW_ACTION_AI_ERROR         1002


/* ---- attack/Attack.java ----------------------------------------- */

#define LW_ATTACK_USE_CRITICAL                  2
#define LW_ATTACK_USE_SUCCESS                   1
#define LW_ATTACK_USE_FAILED                    0
#define LW_ATTACK_USE_INVALID_TARGET           -1
#define LW_ATTACK_USE_NOT_ENOUGH_TP            -2
#define LW_ATTACK_USE_INVALID_COOLDOWN         -3
#define LW_ATTACK_USE_INVALID_POSITION         -4
#define LW_ATTACK_USE_TOO_MANY_SUMMONS         -5
#define LW_ATTACK_USE_RESURRECT_INVALID_ENTITY -6
#define LW_ATTACK_USE_MAX_USES                 -7

#define LW_ATTACK_LAUNCH_TYPE_LINE              1
#define LW_ATTACK_LAUNCH_TYPE_DIAGONAL          2
#define LW_ATTACK_LAUNCH_TYPE_STAR              3
#define LW_ATTACK_LAUNCH_TYPE_STAR_INVERTED     4
#define LW_ATTACK_LAUNCH_TYPE_DIAGONAL_INVERTED 5
#define LW_ATTACK_LAUNCH_TYPE_LINE_INVERTED     6
#define LW_ATTACK_LAUNCH_TYPE_CIRCLE            7

#define LW_ATTACK_TYPE_WEAPON                   1
#define LW_ATTACK_TYPE_CHIP                     2


/* ---- attack/EntityState.java ------------------------------------ */

#define LW_ENTITY_STATE_NONE                 0
#define LW_ENTITY_STATE_RESURRECTED          1
#define LW_ENTITY_STATE_UNHEALABLE           2
#define LW_ENTITY_STATE_INVINCIBLE           3
#define LW_ENTITY_STATE_PACIFIST             4
#define LW_ENTITY_STATE_HEAVY                5
#define LW_ENTITY_STATE_DENSE                6
#define LW_ENTITY_STATE_MAGNETIZED           7
#define LW_ENTITY_STATE_CHAINED              8
#define LW_ENTITY_STATE_ROOTED               9
#define LW_ENTITY_STATE_PETRIFIED           10
#define LW_ENTITY_STATE_STATIC              11


/* ---- effect/Effect.java ----------------------------------------- */

/* Effect type ids (1-indexed, matches the Java effects[] table) */
#define LW_EFFECT_TYPE_DAMAGE                       1
#define LW_EFFECT_TYPE_HEAL                         2
#define LW_EFFECT_TYPE_BUFF_STRENGTH                3
#define LW_EFFECT_TYPE_BUFF_AGILITY                 4
#define LW_EFFECT_TYPE_RELATIVE_SHIELD              5
#define LW_EFFECT_TYPE_ABSOLUTE_SHIELD              6
#define LW_EFFECT_TYPE_BUFF_MP                      7
#define LW_EFFECT_TYPE_BUFF_TP                      8
#define LW_EFFECT_TYPE_DEBUFF                       9
#define LW_EFFECT_TYPE_TELEPORT                    10
#define LW_EFFECT_TYPE_PERMUTATION                 11
#define LW_EFFECT_TYPE_VITALITY                    12
#define LW_EFFECT_TYPE_POISON                      13
#define LW_EFFECT_TYPE_SUMMON                      14
#define LW_EFFECT_TYPE_RESURRECT                   15
#define LW_EFFECT_TYPE_KILL                        16
#define LW_EFFECT_TYPE_SHACKLE_MP                  17
#define LW_EFFECT_TYPE_SHACKLE_TP                  18
#define LW_EFFECT_TYPE_SHACKLE_STRENGTH            19
#define LW_EFFECT_TYPE_DAMAGE_RETURN               20
#define LW_EFFECT_TYPE_BUFF_RESISTANCE             21
#define LW_EFFECT_TYPE_BUFF_WISDOM                 22
#define LW_EFFECT_TYPE_ANTIDOTE                    23
#define LW_EFFECT_TYPE_SHACKLE_MAGIC               24
#define LW_EFFECT_TYPE_AFTEREFFECT                 25
#define LW_EFFECT_TYPE_VULNERABILITY               26
#define LW_EFFECT_TYPE_ABSOLUTE_VULNERABILITY      27
#define LW_EFFECT_TYPE_LIFE_DAMAGE                 28
#define LW_EFFECT_TYPE_STEAL_ABSOLUTE_SHIELD       29
#define LW_EFFECT_TYPE_NOVA_DAMAGE                 30
#define LW_EFFECT_TYPE_RAW_BUFF_MP                 31
#define LW_EFFECT_TYPE_RAW_BUFF_TP                 32
/* 33-36 unused (null in Java effects[]) */
#define LW_EFFECT_TYPE_RAW_ABSOLUTE_SHIELD         37
#define LW_EFFECT_TYPE_RAW_BUFF_STRENGTH           38
#define LW_EFFECT_TYPE_RAW_BUFF_MAGIC              39
#define LW_EFFECT_TYPE_RAW_BUFF_SCIENCE            40
#define LW_EFFECT_TYPE_RAW_BUFF_AGILITY            41
#define LW_EFFECT_TYPE_RAW_BUFF_RESISTANCE         42
/* 43 unused */
#define LW_EFFECT_TYPE_RAW_BUFF_WISDOM             44
#define LW_EFFECT_TYPE_NOVA_VITALITY               45
#define LW_EFFECT_TYPE_ATTRACT                     46
#define LW_EFFECT_TYPE_SHACKLE_AGILITY             47
#define LW_EFFECT_TYPE_SHACKLE_WISDOM              48
#define LW_EFFECT_TYPE_REMOVE_SHACKLES             49
/* 50 unused */
#define LW_EFFECT_TYPE_PUSH                        51
#define LW_EFFECT_TYPE_RAW_BUFF_POWER              52
#define LW_EFFECT_TYPE_REPEL                       53
#define LW_EFFECT_TYPE_RAW_RELATIVE_SHIELD         54
/* 55-56 unused */
#define LW_EFFECT_TYPE_RAW_HEAL                    57
/* 58 unused */
#define LW_EFFECT_TYPE_ADD_STATE                   59
#define LW_EFFECT_TYPE_TOTAL_DEBUFF                60
#define LW_EFFECT_TYPE_STEAL_LIFE                  61
#define LW_EFFECT_TYPE_MULTIPLY_STATS              62

#define LW_EFFECT_COUNT                            62  /* table size */

/* For the Effect.PROPAGATION pseudo-effect (value emitted via
 * action stream but not stored as a stat). */
#define LW_EFFECT_TYPE_PROPAGATION                 43

/* Target filters (bitfield, exact match Java) */
#define LW_TARGET_ENEMIES         1
#define LW_TARGET_ALLIES          2
#define LW_TARGET_CASTER          4
#define LW_TARGET_NON_SUMMONS     8
#define LW_TARGET_SUMMONS        16

/* Modifiers (bitfield, exact match Java) */
#define LW_MODIFIER_STACKABLE                  1
#define LW_MODIFIER_MULTIPLIED_BY_TARGETS      2
#define LW_MODIFIER_ON_CASTER                  4
#define LW_MODIFIER_NOT_REPLACEABLE            8
#define LW_MODIFIER_IRREDUCTIBLE              16

/* Crit + erosion */
#define LW_CRITICAL_FACTOR_NUM           13   /* Effect.CRITICAL_FACTOR = 1.3 */
#define LW_CRITICAL_FACTOR_DEN           10
#define LW_CRITICAL_FACTOR_F             (1.3)
#define LW_EROSION_DAMAGE_F              (0.05)
#define LW_EROSION_POISON_F              (0.10)
#define LW_EROSION_CRITICAL_BONUS_F      (0.10)


/* ---- state/Entity.java ------------------------------------------ */

#define LW_STAT_LIFE              0
#define LW_STAT_TP                1
#define LW_STAT_MP                2
#define LW_STAT_STRENGTH          3
#define LW_STAT_AGILITY           4
#define LW_STAT_FREQUENCY         5
#define LW_STAT_WISDOM            6
/* 7-8 unused */
#define LW_STAT_ABSOLUTE_SHIELD   9
#define LW_STAT_RELATIVE_SHIELD  10
#define LW_STAT_RESISTANCE       11
#define LW_STAT_SCIENCE          12
#define LW_STAT_MAGIC            13
#define LW_STAT_DAMAGE_RETURN    14
#define LW_STAT_POWER            15
#define LW_STAT_CORES            16
#define LW_STAT_RAM              17

#define LW_STAT_COUNT            18

#define LW_ENTITY_TYPE_LEEK      0
#define LW_ENTITY_TYPE_BULB      1
#define LW_ENTITY_TYPE_TURRET    2
#define LW_ENTITY_TYPE_CHEST     3
#define LW_ENTITY_TYPE_MOB       4

#define LW_ENTITY_SAY_LIMIT_TURN  2
#define LW_ENTITY_SHOW_LIMIT_TURN 5


/* ---- state/State.java ------------------------------------------- */

/* Fight states */
#define LW_FIGHT_STATE_INIT       0
#define LW_FIGHT_STATE_RUNNING    1
#define LW_FIGHT_STATE_FINISHED   2

/* Fight types */
#define LW_FIGHT_TYPE_SOLO          0
#define LW_FIGHT_TYPE_FARMER        1
#define LW_FIGHT_TYPE_TEAM          2
#define LW_FIGHT_TYPE_BATTLE_ROYALE 3
#define LW_FIGHT_TYPE_WAR           5
#define LW_FIGHT_TYPE_CHEST_HUNT    6
#define LW_FIGHT_TYPE_COLOSSUS      7

/* Fight contexts */
#define LW_CONTEXT_TEST             0
#define LW_CONTEXT_CHALLENGE        1
#define LW_CONTEXT_GARDEN           2
#define LW_CONTEXT_TOURNAMENT       3
#define LW_CONTEXT_BATTLE_ROYALE    5

#define LW_FIGHT_MAX_TURNS         64
#define LW_SUMMON_LIMIT             8


/* ---- maps/Pathfinding.java -------------------------------------- */
/* Direction codes used by Map.getCellByDir */

#define LW_DIR_NORTH              0
#define LW_DIR_EAST               1
#define LW_DIR_SOUTH              2
#define LW_DIR_WEST               3

/* (Java's Pathfinding.NORTH/EAST/SOUTH/WEST -- verified against the
 * original source.) */


#endif /* LW_CONSTANTS_H */
