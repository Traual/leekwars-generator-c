/*
 * lw_effect_damage_return.c -- 1:1 port of effect/EffectDamageReturn.java
 */
#include <math.h>
#include <stddef.h>

#include "lw_effect.h"
#include "lw_constants.h"
#include "lw_util.h"


/* @Override
 * public void apply(State state) { ... }
 */
void lw_effect_damage_return_apply(LwEffect *self, struct LwState *state) {
    (void) state;

    /* Java:
     * value = (int) Math.round((value1 + jet * value2)
     *           * (1 + caster.getAgility() / 100.0)
     *           * aoe * criticalPower);
     */
    self->value = (int) lw_java_round((self->value1 + self->jet * self->value2)
                                      * (1.0 + (double) lw_entity_get_agility(self->caster) / 100.0)
                                      * self->aoe * self->critical_power);
    if (self->value > 0) {
        lw_stats_set_stat(&self->stats, LW_STAT_DAMAGE_RETURN, self->value);
        lw_entity_update_buff_stats_with(self->target, LW_STAT_DAMAGE_RETURN, self->value, self->caster);
    }
}
