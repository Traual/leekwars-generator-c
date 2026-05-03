/*
 * lw_statistics.h -- 1:1 port of statistics/StatisticsManager.java (interface)
 *
 * Java is a Java interface with ~50 callbacks invoked by State / Entity /
 * Effect at well-defined hooks (kill, damage, useChip, summon, etc.).
 *
 * Approach in C: a struct with one function-pointer per callback so
 * different backends (NoOp / Farmer / RecordingForTests) plug in by
 * filling the same vtable.  The default implementation lives in
 * lw_statistics.c and is a no-op (DefaultStatisticsManager equivalent).
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/statistics/StatisticsManager.java
 */
#ifndef LW_STATISTICS_H
#define LW_STATISTICS_H

#include <stdint.h>

/* Forward decls -- cross-module. */
struct LwEntity;
struct LwAttack;
struct LwChip;
struct LwWeapon;
struct LwEffect;
struct LwItem;
struct LwCell;


/* DamageType (Java attack/DamageType.java) -- only used as a tag here. */
typedef int LwDamageType;


/* The callback table.  Each entry mirrors one Java interface method.
 * Implementations are free to leave entries NULL when they have no body
 * (the dispatchers below null-check before calling). */
typedef struct LwStatisticsManager {

    void  (*init)             (struct LwStatisticsManager *self, struct LwEntity *entity);
    void  (*say)              (struct LwStatisticsManager *self, struct LwEntity *entity, const char *message);
    void  (*teleportation)    (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster, struct LwCell *start, struct LwCell *end, int item_id);
    void  (*lama)             (struct LwStatisticsManager *self, struct LwEntity *entity);
    void  (*characteristics)  (struct LwStatisticsManager *self, struct LwEntity *entity);
    void  (*update_stat)      (struct LwStatisticsManager *self, struct LwEntity *entity, int characteristic, int delta, struct LwEntity *caster);
    void  (*too_much_operations)(struct LwStatisticsManager *self, struct LwEntity *entity);
    void  (*stack_overflow)   (struct LwStatisticsManager *self, struct LwEntity *entity);
    void  (*damage)           (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *attacker, int damage, LwDamageType direct, struct LwEffect *effect);
    void  (*summon)           (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *summon);
    void  (*use_tp)           (struct LwStatisticsManager *self, int tp);
    void  (*heal)             (struct LwStatisticsManager *self, struct LwEntity *healer, struct LwEntity *entity, int pv);
    void  (*error)            (struct LwStatisticsManager *self, struct LwEntity *entity);
    void  (*use_chip)         (struct LwStatisticsManager *self, struct LwEntity *caster, struct LwChip *chip, struct LwCell *cell, struct LwEntity **targets, int n_targets, struct LwEntity *cell_entity);
    void  (*use_weapon)       (struct LwStatisticsManager *self, struct LwEntity *caster, struct LwWeapon *weapon, struct LwCell *cell, struct LwEntity **targets, int n_targets, struct LwEntity *cell_entity);
    void  (*kill)             (struct LwStatisticsManager *self, struct LwEntity *killer, struct LwEntity *entity, struct LwItem *item, struct LwCell *kill_cell);
    void  (*critical)         (struct LwStatisticsManager *self, struct LwEntity *launcher);
    void  (*end_fight)        (struct LwStatisticsManager *self, struct LwEntity **values, int n_values);
    void  (*add_times)        (struct LwStatisticsManager *self, struct LwEntity *current, int64_t time, int64_t operations);
    void  (*move)             (struct LwStatisticsManager *self, struct LwEntity *mover, struct LwEntity *entity, struct LwCell *start, struct LwCell **path, int n_path);
    void  (*resurrect)        (struct LwStatisticsManager *self, struct LwEntity *caster, struct LwEntity *target);

    int   (*get_kills)        (struct LwStatisticsManager *self);
    int   (*get_bullets)      (struct LwStatisticsManager *self);
    int64_t (*get_used_chips) (struct LwStatisticsManager *self);
    int64_t (*get_summons)    (struct LwStatisticsManager *self);
    int64_t (*get_direct_damage)(struct LwStatisticsManager *self);
    int64_t (*get_heal)       (struct LwStatisticsManager *self);
    int64_t (*get_distance)   (struct LwStatisticsManager *self);
    int64_t (*get_stack_overflow)(struct LwStatisticsManager *self);
    int64_t (*get_errors)     (struct LwStatisticsManager *self);
    int64_t (*get_resurrects) (struct LwStatisticsManager *self);
    int64_t (*get_damage_poison)(struct LwStatisticsManager *self);
    int64_t (*get_damage_return)(struct LwStatisticsManager *self);
    int64_t (*get_critical_hits)(struct LwStatisticsManager *self);
    int64_t (*get_tp_used)    (struct LwStatisticsManager *self);
    int64_t (*get_mp_used)    (struct LwStatisticsManager *self);
    int64_t (*get_operations) (struct LwStatisticsManager *self);
    int64_t (*get_says)       (struct LwStatisticsManager *self);
    int64_t (*get_says_length)(struct LwStatisticsManager *self);

    void  (*too_much_debug)   (struct LwStatisticsManager *self, int farmer);
    void  (*show)             (struct LwStatisticsManager *self, struct LwEntity *entity, int cell_id);
    void  (*slide)            (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster, struct LwCell *start, struct LwCell *cell);
    void  (*use_invalid_position)(struct LwStatisticsManager *self, struct LwEntity *caster, struct LwAttack *attack, struct LwCell *target);
    void  (*effect)           (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster, struct LwEffect *effect);
    void  (*entity_turn)      (struct LwStatisticsManager *self, struct LwEntity *entity);
    void  (*antidote)         (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster, int poisons_removed);
    void  (*vitality)         (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster, int vitality);
    void  (*register_write)   (struct LwStatisticsManager *self, struct LwEntity *entity, const char *key, const char *value);
    void  (*set_weapon)       (struct LwStatisticsManager *self, struct LwEntity *entity, struct LwWeapon *w);
    void  (*chest)            (struct LwStatisticsManager *self);
    void  (*chest_killed)     (struct LwStatisticsManager *self, struct LwEntity *killer, struct LwEntity *entity);

    int64_t (*get_chests)     (struct LwStatisticsManager *self);
    int64_t (*get_chests_kills)(struct LwStatisticsManager *self);

    /* Backend-private state (cast in implementation). */
    void *user_data;
} LwStatisticsManager;


/* Initialise the manager with the no-op default vtable (= Java
 * "DefaultStatisticsManager"-style stub).  Every function pointer is
 * either NULL (no-op) or a getter returning 0. */
void lw_statistics_init_default(LwStatisticsManager *self);


/* ---- Convenience dispatch helpers ----------------------------------- *
 *
 * State.java calls statistics.kill(...), statistics.useChip(...), etc.
 * Wrap each as a free function that null-checks the table and the slot,
 * so the State port can call lw_stats_dispatch_* unconditionally.
 */
static inline void lw_stats_kill(LwStatisticsManager *self, struct LwEntity *killer,
                                 struct LwEntity *entity, struct LwItem *item, struct LwCell *kill_cell) {
    if (self && self->kill) self->kill(self, killer, entity, item, kill_cell);
}
static inline void lw_stats_chest(LwStatisticsManager *self) {
    if (self && self->chest) self->chest(self);
}
static inline void lw_stats_chest_killed(LwStatisticsManager *self, struct LwEntity *killer, struct LwEntity *entity) {
    if (self && self->chest_killed) self->chest_killed(self, killer, entity);
}
static inline void lw_stats_set_weapon(LwStatisticsManager *self, struct LwEntity *entity, struct LwWeapon *w) {
    if (self && self->set_weapon) self->set_weapon(self, entity, w);
}
static inline void lw_stats_use_weapon(LwStatisticsManager *self, struct LwEntity *caster, struct LwWeapon *weapon,
                                       struct LwCell *cell, struct LwEntity **targets, int n_targets,
                                       struct LwEntity *cell_entity) {
    if (self && self->use_weapon) self->use_weapon(self, caster, weapon, cell, targets, n_targets, cell_entity);
}
static inline void lw_stats_use_chip(LwStatisticsManager *self, struct LwEntity *caster, struct LwChip *chip,
                                     struct LwCell *cell, struct LwEntity **targets, int n_targets,
                                     struct LwEntity *cell_entity) {
    if (self && self->use_chip) self->use_chip(self, caster, chip, cell, targets, n_targets, cell_entity);
}
static inline void lw_stats_critical(LwStatisticsManager *self, struct LwEntity *launcher) {
    if (self && self->critical) self->critical(self, launcher);
}
static inline void lw_stats_use_invalid_position(LwStatisticsManager *self, struct LwEntity *caster,
                                                  struct LwAttack *attack, struct LwCell *target) {
    if (self && self->use_invalid_position) self->use_invalid_position(self, caster, attack, target);
}
static inline void lw_stats_summon(LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *summon) {
    if (self && self->summon) self->summon(self, entity, summon);
}
static inline void lw_stats_resurrect(LwStatisticsManager *self, struct LwEntity *caster, struct LwEntity *target) {
    if (self && self->resurrect) self->resurrect(self, caster, target);
}
static inline void lw_stats_move(LwStatisticsManager *self, struct LwEntity *mover, struct LwEntity *entity,
                                 struct LwCell *start, struct LwCell **path, int n_path) {
    if (self && self->move) self->move(self, mover, entity, start, path, n_path);
}
static inline void lw_stats_slide(LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster,
                                  struct LwCell *start, struct LwCell *cell) {
    if (self && self->slide) self->slide(self, entity, caster, start, cell);
}
static inline void lw_stats_teleportation(LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster,
                                          struct LwCell *start, struct LwCell *end, int item_id) {
    if (self && self->teleportation) self->teleportation(self, entity, caster, start, end, item_id);
}
static inline void lw_stats_effect(LwStatisticsManager *self, struct LwEntity *entity, struct LwEntity *caster,
                                   struct LwEffect *effect) {
    if (self && self->effect) self->effect(self, entity, caster, effect);
}


#endif /* LW_STATISTICS_H */
