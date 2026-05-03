/*
 * lw_statistics.c -- default no-op StatisticsManager.
 *
 * The Java codebase ships StatisticsManager as an interface and uses
 * concrete implementations (FarmerStatistics for the live engine, a
 * silent stub for tests).  We provide the silent stub here; richer
 * backends register a different vtable.
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/statistics/StatisticsManager.java
 */
#include "lw_statistics.h"

#include <stddef.h>


static int     noop_get_int   (struct LwStatisticsManager *self) { (void)self; return 0; }
static int64_t noop_get_long  (struct LwStatisticsManager *self) { (void)self; return 0; }


/* Initialise a no-op manager: every callback is NULL, every getter
 * returns 0. Suitable as the default when the caller doesn't care about
 * stats. */
void lw_statistics_init_default(LwStatisticsManager *self) {
    if (!self) return;

    self->init                 = NULL;
    self->say                  = NULL;
    self->teleportation        = NULL;
    self->lama                 = NULL;
    self->characteristics      = NULL;
    self->update_stat          = NULL;
    self->too_much_operations  = NULL;
    self->stack_overflow       = NULL;
    self->damage               = NULL;
    self->summon               = NULL;
    self->use_tp               = NULL;
    self->heal                 = NULL;
    self->error                = NULL;
    self->use_chip             = NULL;
    self->use_weapon           = NULL;
    self->kill                 = NULL;
    self->critical             = NULL;
    self->end_fight            = NULL;
    self->add_times            = NULL;
    self->move                 = NULL;
    self->resurrect            = NULL;

    self->get_kills            = noop_get_int;
    self->get_bullets          = noop_get_int;
    self->get_used_chips       = noop_get_long;
    self->get_summons          = noop_get_long;
    self->get_direct_damage    = noop_get_long;
    self->get_heal             = noop_get_long;
    self->get_distance         = noop_get_long;
    self->get_stack_overflow   = noop_get_long;
    self->get_errors           = noop_get_long;
    self->get_resurrects       = noop_get_long;
    self->get_damage_poison    = noop_get_long;
    self->get_damage_return    = noop_get_long;
    self->get_critical_hits    = noop_get_long;
    self->get_tp_used          = noop_get_long;
    self->get_mp_used          = noop_get_long;
    self->get_operations       = noop_get_long;
    self->get_says             = noop_get_long;
    self->get_says_length      = noop_get_long;

    self->too_much_debug       = NULL;
    self->show                 = NULL;
    self->slide                = NULL;
    self->use_invalid_position = NULL;
    self->effect               = NULL;
    self->entity_turn          = NULL;
    self->antidote             = NULL;
    self->vitality             = NULL;
    self->register_write       = NULL;
    self->set_weapon           = NULL;
    self->chest                = NULL;
    self->chest_killed         = NULL;

    self->get_chests           = noop_get_long;
    self->get_chests_kills     = noop_get_long;

    self->user_data            = NULL;
}
