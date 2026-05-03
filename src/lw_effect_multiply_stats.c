/*
 * lw_effect_multiply_stats.c -- 1:1 port of effect/EffectMultiplyStats.java
 *
 * Multiplies all stats of the target by value1.
 * Used for Colossus mode to boost the colossus's stats.
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_multiply_stats_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    int factor = (int) self->value1;
    if (factor <= 1) return;

    self->value = factor;

    /* Multiply all base stats (except life, handled separately) */
    /* Java:
     * int[] statIds = {
     *   Entity.STAT_STRENGTH, Entity.STAT_AGILITY,
     *   Entity.STAT_RESISTANCE, Entity.STAT_WISDOM, Entity.STAT_SCIENCE,
     *   Entity.STAT_MAGIC, Entity.STAT_FREQUENCY, Entity.STAT_TP, Entity.STAT_MP
     * };
     */
    static const int statIds[] = {
        LW_STAT_STRENGTH, LW_STAT_AGILITY,
        LW_STAT_RESISTANCE, LW_STAT_WISDOM, LW_STAT_SCIENCE,
        LW_STAT_MAGIC, LW_STAT_FREQUENCY, LW_STAT_TP, LW_STAT_MP
    };
    int n_stat_ids = (int) (sizeof(statIds) / sizeof(statIds[0]));

    LwStats *base_stats = lw_entity_get_base_stats(self->target);
    for (int i = 0; i < n_stat_ids; ++i) {
        int statId = statIds[i];
        int base = lw_stats_get_stat(base_stats, statId);
        int buff = base * (factor - 1);
        if (buff > 0) {
            lw_stats_set_stat(&self->stats, statId, buff);
            lw_entity_update_buff_stats_with(self->target, statId, buff, self->caster);
        }
    }

    /* Life: add 1x base life to max life per factor, preserving ratio and erosion.
     * On first apply (factor=5): add 4x base life
     * On replacement (factor=6 after remove of factor=5): mTotalLife still has
     * the old bonus since removeEffect doesn't undo addTotalLife. The old bonus
     * was (oldFactor-1)*base, so mTotalLife = base*oldFactor - erosion.
     * We need to add exactly 1x base (the delta between factors).
     * For the first apply, there's no previous effect, so we add (factor-1)*base.
     * We detect first apply by checking if mTotalLife <= lifeBase (no prior boost).
     */
    int lifeBase = lw_stats_get_stat(base_stats, LW_STAT_LIFE);
    int lifeDelta;
    if (lw_entity_get_total_life(self->target) <= lifeBase) {
        /* First apply: no previous boost */
        lifeDelta = lifeBase * (factor - 1);
    } else {
        /* Replacement: previous boost still in mTotalLife, just add 1x base */
        lifeDelta = lifeBase;
    }

    double ratio = lw_entity_get_total_life(self->target) > 0
                 ? (double) lw_entity_get_life(self->target) / (double) lw_entity_get_total_life(self->target)
                 : 1.0;
    lw_entity_add_total_life(self->target, lifeDelta, self->caster);
    int targetLife = (int) lw_java_round((double) lw_entity_get_total_life(self->target) * ratio);
    int healAmount = targetLife - lw_entity_get_life(self->target);
    if (healAmount > 0) {
        lw_entity_add_life(self->target, self->caster, healAmount);
    }
}
