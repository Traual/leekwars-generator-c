/*
 * lw_effect_params.h -- 1:1 port of effect/EffectParameters.java
 *
 * Java:   public class EffectParameters { final int id, value1, value2, turns, targets, modifiers; }
 * C:      LwEffectParameters value-type struct (held inline by Item / Attack
 *         descriptors; never heap-allocated on its own).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/effect/EffectParameters.java
 */
#ifndef LW_EFFECT_PARAMS_H
#define LW_EFFECT_PARAMS_H

typedef struct {
    int    id;          /* private final int id */
    double value1;      /* private final double value1 */
    double value2;      /* private final double value2 */
    int    turns;       /* private final int turns */
    int    targets;     /* private final int targets */
    int    modifiers;   /* private final int modifiers */
} LwEffectParameters;


/* public EffectParameters(int id, double value1, double value2, int turns, int targets, int modifiers) */
static inline void lw_effect_parameters_init(LwEffectParameters *self,
                                             int id, double value1, double value2,
                                             int turns, int targets, int modifiers) {
    self->id = id;
    self->value1 = value1;
    self->value2 = value2;
    self->turns = turns;
    self->targets = targets;
    self->modifiers = modifiers;
}

/* Trivial getters -- inlined to mirror Java's getId / getValue1 / ... */
static inline int    lw_effect_parameters_get_id       (const LwEffectParameters *self) { return self->id; }
static inline double lw_effect_parameters_get_value1   (const LwEffectParameters *self) { return self->value1; }
static inline double lw_effect_parameters_get_value2   (const LwEffectParameters *self) { return self->value2; }
static inline int    lw_effect_parameters_get_turns    (const LwEffectParameters *self) { return self->turns; }
static inline int    lw_effect_parameters_get_targets  (const LwEffectParameters *self) { return self->targets; }
static inline int    lw_effect_parameters_get_modifiers(const LwEffectParameters *self) { return self->modifiers; }


#endif /* LW_EFFECT_PARAMS_H */
