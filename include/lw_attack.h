/*
 * lw_attack.h -- Attack profile (range, launch type, area, damage).
 *
 * Weapons and chips both wrap an Attack. We don't load the catalog
 * here -- that's the Python/scenario layer's job. The C engine just
 * receives populated LwAttack values when an action is applied.
 *
 * Launch type (bitmask):
 *   bit 1 = line allowed       (dx == 0 or dy == 0)
 *   bit 2 = diagonal allowed   (|dx| == |dy| and dx != 0)
 *   bit 4 = "any" allowed      (off-line and off-diagonal)
 *
 * Area types we treat specially:
 *   FIRST_IN_LINE = 13 -- LoS skips the first entity in the line.
 *   Others are handled by the damage routine (AoE shape), not LoS.
 */
#ifndef LW_ATTACK_H
#define LW_ATTACK_H

#include "lw_types.h"

#define LW_AREA_FIRST_IN_LINE  13

typedef struct {
    int min_range;
    int max_range;
    int launch_type;
    int area;            /* LW_AREA_* (rarely affects LoS) */
    int needs_los;       /* boolean */

    /* Damage / effect parameters used by the apply routines */
    double value1, value2;     /* min / max damage roll */
    int    n_effects;          /* number of EffectParam records below */
    /* For now, simple single-effect attacks only. */
    int    effect_id;
    double effect_v1, effect_v2;
    int    effect_turns;
} LwAttack;

#endif /* LW_ATTACK_H */
