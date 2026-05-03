/*
 * lw_effect.c -- Effect base + factory + virtual dispatch.
 *
 * Source: java_reference/src/main/java/com/leekwars/generator/effect/Effect.java
 *
 * Per-subclass apply() implementations live in lw_effect_<name>.c. This
 * file holds:
 *   - lw_effect_init                   : default-construct an LwEffect
 *   - lw_effect_create_effect          : the static factory (Effect.java:188)
 *   - lw_effect_apply / _start_turn    : id → subclass switch
 *   - lw_effect_add_log                : Effect.java:266
 *   - lw_effect_reduce                 : Effect.java:329
 *   - lw_effect_merge_with             : Effect.java:341
 *   - lw_effect_get_effect_stat        : Effect.java:354
 *   - lw_effect_get_item               : Effect.java:403
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_actions.h"      /* lw_actions_log_stack_effect, lw_actions_log_add_effect */
#include "lw_attack.h"       /* lw_attack_get_type, lw_attack_get_item_id */
#include "lw_state.h"        /* LwState, lw_state_get_actions, lw_state_invert_entities */
#include "lw_statistics.h"   /* lw_stats_effect inline helper */

#include "lw_util.h"         /* lw_java_round, lw_java_signum, lw_max_d */


/* ------------------------------------------------------------------- */
/* default-construct: matches Java field initialisers on Effect.       */
/* ------------------------------------------------------------------- */
void lw_effect_init(LwEffect *self) {
    self->id = 0;
    self->turns = 0;                 /* protected int turns = 0; */
    self->aoe = 1.0;                 /* protected double aoe = 1.0; */
    self->value1 = 0.0;
    self->value2 = 0.0;
    self->critical = 0;              /* protected boolean critical = false; */
    self->critical_power = 1.0;      /* protected double criticalPower = 1.0; */
    self->caster = NULL;
    self->target = NULL;
    self->attack = NULL;
    self->jet = 0.0;
    lw_stats_init(&self->stats);     /* protected Stats stats = new Stats(); */
    self->log_id = 0;                /* protected int logID = 0; */
    self->erosion_rate = 0.0;
    self->value = 0;                 /* public int value = 0; */
    self->previous_effect_total_value = 0;
    self->target_count = 0;
    self->propagate = 0;             /* public int propagate = 0; */
    self->modifiers = 0;             /* public int modifiers = 0; */
    self->state = LW_ENTITY_STATE_NONE;
}


/* ------------------------------------------------------------------- */
/* Java: public static int createEffect(State state, int id, int turns, */
/*       double aoe, double value1, double value2, boolean critical,    */
/*       Entity target, Entity caster, Attack attack, double jet,       */
/*       boolean stackable, int previousEffectTotalValue, int targetCount,*/
/*       int propagate, int modifiers)                                  */
/*                                                                      */
/* Effect.java:188                                                      */
/* ------------------------------------------------------------------- */

/* The Java code allocates a fresh Effect (via reflection) per call.  In
 * C, we let the State carry a per-fight pool of LwEffect slots and ask
 * for one here.  The exact pool API lives in lw_state.h once that file
 * is ported -- declared as forward here. */
LwEffect* lw_state_alloc_effect(struct LwState *state);

int lw_effect_create_effect(struct LwState *state, int id, int turns, double aoe,
                            double value1, double value2, int critical,
                            struct LwEntity *target, struct LwEntity *caster,
                            LwAttack *attack, double jet, int stackable,
                            int previous_effect_total_value, int target_count,
                            int propagate, int modifiers) {

    /* Invalid effect id */
    /* Java: if (id < 0 || id > effects.length) return 0;
     * effects.length == 62 -- but the original test is `>` not `>=` so
     * id == 62 is allowed; id 0 is rejected because effects[id-1] would
     * underflow.  We keep the bug-for-bug match. */
    if (id < 0 || id > LW_EFFECT_COUNT) {
        return 0;
    }

    /* Create the effect */
    LwEffect *effect;
    /* Java: try { effect = (Effect) effects[id - 1].getDeclaredConstructor().newInstance(); }
     *       catch (Exception e) { return 0; }
     *
     * In C, the failure mode is "id maps to a null slot" (33-36, 43, 50,
     * 55-56, 58) -- in Java that NPEs at .getDeclaredConstructor and the
     * catch returns 0.  We emulate by checking the slot validity here. */
    switch (id) {
        case 33: case 34: case 35: case 36:
        case 43:
        case 50:
        case 55: case 56:
        case 58:
            return 0;          /* null slot in Java effects[] */
        default:
            break;
    }
    effect = lw_state_alloc_effect(state);
    if (effect == NULL) {
        return 0;
    }
    lw_effect_init(effect);

    lw_effect_set_id(effect, id);
    effect->turns = turns;
    effect->aoe = aoe;
    effect->value1 = value1;
    effect->value2 = value2;
    effect->critical = critical;
    effect->critical_power = critical ? LW_CRITICAL_FACTOR_F : 1.0;
    effect->caster = caster;
    effect->target = target;
    effect->attack = attack;
    effect->jet = jet;
    effect->erosion_rate = (id == LW_EFFECT_TYPE_POISON) ? LW_EROSION_POISON_F : LW_EROSION_DAMAGE_F;
    if (critical) effect->erosion_rate += LW_EROSION_CRITICAL_BONUS_F;
    effect->previous_effect_total_value = previous_effect_total_value;
    effect->target_count = target_count;
    effect->propagate = propagate;
    effect->modifiers = modifiers;

    /* Remove previous effect of the same type (that is not stackable) */
    if (lw_effect_get_turns(effect) != 0) {
        if (!stackable) {
            int n = lw_entity_get_effects_count(target);
            for (int i = 0; i < n; ++i) {
                LwEffect *e = lw_entity_get_effect_at(target, i);
                /* Java:
                 *   if (e.getId() == id && (e.attack == null
                 *       ? attack == null
                 *       : attack != null && e.attack.getItemId() == attack.getItemId())) { ... break; }
                 *
                 * Note Java's ternary here is asymmetric: when e.attack
                 * != null and attack == null, the second arm is reached
                 * with attack==null, so attack != null is false and the
                 * test fails.  We mirror exactly. */
                int same;
                if (e->attack == NULL) {
                    same = (attack == NULL);
                } else {
                    same = (attack != NULL) &&
                           (lw_attack_get_item_id(e->attack) == lw_attack_get_item_id(attack));
                }
                if (lw_effect_get_id(e) == id && same) {
                    lw_entity_remove_launched_effect(lw_effect_get_caster(e), e);
                    lw_entity_remove_effect(target, e);
                    break;
                }
            }
        }
    }

    /* Compute the effect */
    lw_effect_apply(effect, state);

    /* Stack to previous item with the same characteristics */
    if (effect->value > 0) {
        int n = lw_entity_get_effects_count(target);
        for (int i = 0; i < n; ++i) {
            LwEffect *e = lw_entity_get_effect_at(target, i);
            int same;
            if (e->attack == NULL) {
                same = (attack == NULL);
            } else {
                same = (attack != NULL) &&
                       (lw_attack_get_item_id(e->attack) == lw_attack_get_item_id(attack));
            }
            if (same && lw_effect_get_id(e) == id && e->turns == turns && e->caster == caster) {
                lw_effect_merge_with(e, effect);
                lw_actions_log_stack_effect(lw_state_get_actions(state),
                                            lw_effect_get_log_id(e),
                                            effect->value);
                return effect->value;        /* No need to apply the effect again */
            }
        }
    }

    /* Add effect to the target and the caster */
    if (lw_effect_get_turns(effect) != 0 && effect->value > 0) {
        lw_entity_add_effect(target, effect);
        lw_entity_add_launched_effect(caster, effect);
        lw_effect_add_log(effect, state);
        /* Java: state.statistics.effect(target, caster, effect); */
        lw_stats_effect(state->statistics, target, caster, effect);
    }
    return effect->value;
}


/* ------------------------------------------------------------------- */
/* Java: public void addLog(State state)   (Effect.java:266)           */
/* ------------------------------------------------------------------- */
void lw_effect_add_log(LwEffect *self, struct LwState *state) {
    if (self->turns == 0) {
        return;
    }
    /* Java:
     *   logID = ActionAddEffect.createEffect(state.getActions(),
     *       attack == null ? Attack.TYPE_CHIP : attack.getType(),
     *       attack == null ? 0 : attack.getItemId(),
     *       caster, target, getId(), value, turns, modifiers);
     */
    int type    = (self->attack == NULL) ? LW_ATTACK_TYPE_CHIP : lw_attack_get_type(self->attack);
    int item_id = (self->attack == NULL) ? 0                   : lw_attack_get_item_id(self->attack);

    self->log_id = lw_actions_log_add_effect(lw_state_get_actions(state),
                                             type, item_id,
                                             self->caster, self->target,
                                             lw_effect_get_id(self),
                                             self->value, self->turns, self->modifiers);
}


/* ------------------------------------------------------------------- */
/* Java: public void reduce(double percent, Entity caster)              */
/*       (Effect.java:329)                                              */
/* ------------------------------------------------------------------- */
void lw_effect_reduce(LwEffect *self, double percent, struct LwEntity *caster) {
    /* Java: double reduction = Math.max(0.0, 1.0 - percent); */
    double reduction = lw_max_d(0.0, 1.0 - percent);

    self->value = (int) lw_java_round((double) self->value * reduction);

    /* Java:
     *   for (var stat : stats.stats.entrySet()) {
     *       int newValue = (int) (Math.round(Math.abs(stat.getValue()) * reduction)
     *                              * Math.signum(stat.getValue()));
     *       int delta = newValue - stat.getValue();
     *       stats.updateStat(stat.getKey(), delta);
     *       target.updateBuffStats(stat.getKey(), delta, caster);
     *   }
     *
     * NOTE: stats.stats is TreeMap<Integer, Integer> -- iteration order is
     * by key ascending.  Our LwStats keeps a dense int[STAT_COUNT]; iterating
     * by index 0..N-1 is the same order, with skipping value==0 cells (Java's
     * TreeMap simply doesn't have those entries, so iteration skips them too).
     */
    for (int k = 0; k < LW_STAT_COUNT; ++k) {
        int v = self->stats.stats[k];
        if (v == 0) continue;
        int abs_v = v < 0 ? -v : v;
        double signum = (v > 0) ? 1.0 : (v < 0 ? -1.0 : 0.0);
        int newValue = (int) (lw_java_round((double) abs_v * reduction) * signum);
        int delta = newValue - v;
        lw_stats_update_stat(&self->stats, k, delta);
        lw_entity_update_buff_stats_with(self->target, k, delta, caster);
    }
}


/* ------------------------------------------------------------------- */
/* Java: public void mergeWith(Effect effect)   (Effect.java:341)       */
/* ------------------------------------------------------------------- */
void lw_effect_merge_with(LwEffect *self, const LwEffect *effect) {
    self->value += effect->value;
    /* Java:
     *   for (var stat : stats.stats.entrySet()) {
     *       int signum = stat.getValue() > 0 ? 1 : -1;
     *       stats.updateStat(stat.getKey(), effect.value * signum);
     *   }
     *
     * Java's TreeMap walks keys ascending; same as our index walk.
     */
    for (int k = 0; k < LW_STAT_COUNT; ++k) {
        int v = self->stats.stats[k];
        if (v == 0) continue;
        int signum = (v > 0) ? 1 : -1;
        lw_stats_update_stat(&self->stats, k, effect->value * signum);
    }
}


/* ------------------------------------------------------------------- */
/* Java: public static int getEffectStat(int type)   (Effect.java:354)  */
/* ------------------------------------------------------------------- */
int lw_effect_get_effect_stat(int type) {
    switch (type) {
        case LW_EFFECT_TYPE_DAMAGE:
            return LW_STAT_STRENGTH;
        case LW_EFFECT_TYPE_POISON:
        case LW_EFFECT_TYPE_SHACKLE_MAGIC:
        case LW_EFFECT_TYPE_SHACKLE_STRENGTH:
        case LW_EFFECT_TYPE_SHACKLE_MP:
        case LW_EFFECT_TYPE_SHACKLE_TP:
            return LW_STAT_MAGIC;
        case LW_EFFECT_TYPE_LIFE_DAMAGE:
            return LW_STAT_LIFE;
        case LW_EFFECT_TYPE_NOVA_DAMAGE:
        case LW_EFFECT_TYPE_BUFF_AGILITY:
        case LW_EFFECT_TYPE_BUFF_STRENGTH:
        case LW_EFFECT_TYPE_BUFF_MP:
        case LW_EFFECT_TYPE_BUFF_TP:
        case LW_EFFECT_TYPE_BUFF_RESISTANCE:
        case LW_EFFECT_TYPE_BUFF_WISDOM:
            return LW_STAT_SCIENCE;
        case LW_EFFECT_TYPE_DAMAGE_RETURN:
            return LW_STAT_AGILITY;
        case LW_EFFECT_TYPE_HEAL:
        case LW_EFFECT_TYPE_VITALITY:
            return LW_STAT_WISDOM;
        case LW_EFFECT_TYPE_RELATIVE_SHIELD:
        case LW_EFFECT_TYPE_ABSOLUTE_SHIELD:
            return LW_STAT_RESISTANCE;
    }
    return -1;
}


/* ------------------------------------------------------------------- */
/* Java: public Item getItem()   (Effect.java:403)                      */
/* ------------------------------------------------------------------- */
struct LwItem* lw_effect_get_item(const LwEffect *self) {
    return self->attack != NULL ? lw_attack_get_item(self->attack) : NULL;
}


/* ------------------------------------------------------------------- */
/* Virtual dispatch -- switch on id                                    */
/*                                                                      */
/* The Java effects[] table maps id (1-indexed) → Class<?>; each Class  */
/* overrides apply() and (for 3 of them) applyStartTurn().              */
/* Below the switch is in 1:1 correspondence with Effect.java:101-164.  */
/* Empty subclasses (Summon, Teleport, Push, Repel, Resurrect, Attract, */
/* AllyKilledToAgility) and the null slots fall through to no-op.       */
/* ------------------------------------------------------------------- */
void lw_effect_apply(LwEffect *self, struct LwState *state) {
    switch (self->id) {
        case LW_EFFECT_TYPE_DAMAGE:                  lw_effect_damage_apply(self, state);              return;
        case LW_EFFECT_TYPE_HEAL:                    lw_effect_heal_apply(self, state);                return;
        case LW_EFFECT_TYPE_BUFF_STRENGTH:           lw_effect_buff_strength_apply(self, state);       return;
        case LW_EFFECT_TYPE_BUFF_AGILITY:            lw_effect_buff_agility_apply(self, state);        return;
        case LW_EFFECT_TYPE_RELATIVE_SHIELD:         lw_effect_relative_shield_apply(self, state);     return;
        case LW_EFFECT_TYPE_ABSOLUTE_SHIELD:         lw_effect_absolute_shield_apply(self, state);     return;
        case LW_EFFECT_TYPE_BUFF_MP:                 lw_effect_buff_mp_apply(self, state);             return;
        case LW_EFFECT_TYPE_BUFF_TP:                 lw_effect_buff_tp_apply(self, state);             return;
        case LW_EFFECT_TYPE_DEBUFF:                  lw_effect_debuff_apply(self, state);              return;
        case LW_EFFECT_TYPE_TELEPORT:                /* empty Java body */                              return;
        case LW_EFFECT_TYPE_PERMUTATION:             lw_effect_permutation_apply(self, state);         return;
        case LW_EFFECT_TYPE_VITALITY:                lw_effect_vitality_apply(self, state);            return;
        case LW_EFFECT_TYPE_POISON:                  lw_effect_poison_apply(self, state);              return;
        case LW_EFFECT_TYPE_SUMMON:                  /* empty Java body */                              return;
        case LW_EFFECT_TYPE_RESURRECT:               /* empty Java body */                              return;
        case LW_EFFECT_TYPE_KILL:                    lw_effect_kill_apply(self, state);                return;
        case LW_EFFECT_TYPE_SHACKLE_MP:              lw_effect_shackle_mp_apply(self, state);          return;
        case LW_EFFECT_TYPE_SHACKLE_TP:              lw_effect_shackle_tp_apply(self, state);          return;
        case LW_EFFECT_TYPE_SHACKLE_STRENGTH:        lw_effect_shackle_strength_apply(self, state);    return;
        case LW_EFFECT_TYPE_DAMAGE_RETURN:           lw_effect_damage_return_apply(self, state);       return;
        case LW_EFFECT_TYPE_BUFF_RESISTANCE:         lw_effect_buff_resistance_apply(self, state);     return;
        case LW_EFFECT_TYPE_BUFF_WISDOM:             lw_effect_buff_wisdom_apply(self, state);         return;
        case LW_EFFECT_TYPE_ANTIDOTE:                lw_effect_antidote_apply(self, state);            return;
        case LW_EFFECT_TYPE_SHACKLE_MAGIC:           lw_effect_shackle_magic_apply(self, state);       return;
        case LW_EFFECT_TYPE_AFTEREFFECT:             lw_effect_aftereffect_apply(self, state);         return;
        case LW_EFFECT_TYPE_VULNERABILITY:           lw_effect_vulnerability_apply(self, state);       return;
        case LW_EFFECT_TYPE_ABSOLUTE_VULNERABILITY:  lw_effect_absolute_vulnerability_apply(self, state); return;
        case LW_EFFECT_TYPE_LIFE_DAMAGE:             lw_effect_life_damage_apply(self, state);         return;
        case LW_EFFECT_TYPE_STEAL_ABSOLUTE_SHIELD:   lw_effect_steal_absolute_shield_apply(self, state); return;
        case LW_EFFECT_TYPE_NOVA_DAMAGE:             lw_effect_nova_damage_apply(self, state);         return;
        case LW_EFFECT_TYPE_RAW_BUFF_MP:             lw_effect_raw_buff_mp_apply(self, state);         return;
        case LW_EFFECT_TYPE_RAW_BUFF_TP:             lw_effect_raw_buff_tp_apply(self, state);         return;
        /* 33-36 null slots in Java effects[] — never reached because
         * createEffect rejects them earlier; keep the no-op for safety. */
        case LW_EFFECT_TYPE_RAW_ABSOLUTE_SHIELD:     lw_effect_raw_absolute_shield_apply(self, state); return;
        case LW_EFFECT_TYPE_RAW_BUFF_STRENGTH:       lw_effect_raw_buff_strength_apply(self, state);   return;
        case LW_EFFECT_TYPE_RAW_BUFF_MAGIC:          lw_effect_raw_buff_magic_apply(self, state);      return;
        case LW_EFFECT_TYPE_RAW_BUFF_SCIENCE:        lw_effect_raw_buff_science_apply(self, state);    return;
        case LW_EFFECT_TYPE_RAW_BUFF_AGILITY:        lw_effect_raw_buff_agility_apply(self, state);    return;
        case LW_EFFECT_TYPE_RAW_BUFF_RESISTANCE:     lw_effect_raw_buff_resistance_apply(self, state); return;
        /* 43 null slot */
        case LW_EFFECT_TYPE_RAW_BUFF_WISDOM:         lw_effect_raw_buff_wisdom_apply(self, state);     return;
        case LW_EFFECT_TYPE_NOVA_VITALITY:           lw_effect_nova_vitality_apply(self, state);       return;
        case LW_EFFECT_TYPE_ATTRACT:                 /* empty Java body */                              return;
        case LW_EFFECT_TYPE_SHACKLE_AGILITY:         lw_effect_shackle_agility_apply(self, state);     return;
        case LW_EFFECT_TYPE_SHACKLE_WISDOM:          lw_effect_shackle_wisdom_apply(self, state);      return;
        case LW_EFFECT_TYPE_REMOVE_SHACKLES:         lw_effect_remove_shackles_apply(self, state);     return;
        /* 50 null slot */
        case LW_EFFECT_TYPE_PUSH:                    /* empty Java body */                              return;
        case LW_EFFECT_TYPE_RAW_BUFF_POWER:          lw_effect_raw_buff_power_apply(self, state);      return;
        case LW_EFFECT_TYPE_REPEL:                   /* empty Java body */                              return;
        case LW_EFFECT_TYPE_RAW_RELATIVE_SHIELD:     lw_effect_raw_relative_shield_apply(self, state); return;
        /* 55-56 null slots */
        case LW_EFFECT_TYPE_RAW_HEAL:                lw_effect_raw_heal_apply(self, state);            return;
        /* 58 null slot */
        case LW_EFFECT_TYPE_ADD_STATE:               lw_effect_add_state_apply(self, state);           return;
        case LW_EFFECT_TYPE_TOTAL_DEBUFF:            lw_effect_total_debuff_apply(self, state);        return;
        case LW_EFFECT_TYPE_STEAL_LIFE:              lw_effect_steal_life_apply(self, state);          return;
        case LW_EFFECT_TYPE_MULTIPLY_STATS:          lw_effect_multiply_stats_apply(self, state);      return;
        default: return;
    }
}


void lw_effect_apply_start_turn(LwEffect *self, struct LwState *state) {
    /* Only three subclasses override applyStartTurn in the Java reference. */
    switch (self->id) {
        case LW_EFFECT_TYPE_HEAL:        lw_effect_heal_apply_start_turn(self, state);        return;
        case LW_EFFECT_TYPE_POISON:      lw_effect_poison_apply_start_turn(self, state);      return;
        case LW_EFFECT_TYPE_AFTEREFFECT: lw_effect_aftereffect_apply_start_turn(self, state); return;
        default: return;          /* base Effect.applyStartTurn = empty */
    }
}
