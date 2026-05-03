/*
 * lw_start_order.h -- 1:1 port of state/StartOrder.java
 *
 * Used at fight init only: bucket entities by team, sort each bucket
 * by Entity.getFrequency() desc, then deal one team per RNG draw to
 * produce the global play order.
 *
 * The RNG draws here are critical for parity (seed-determinism).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/StartOrder.java
 */
#ifndef LW_START_ORDER_H
#define LW_START_ORDER_H

/* Forward decls. */
struct LwEntity;
struct LwState;

/* Capacity bounds (matches LwOrder bound). */
#define LW_START_ORDER_MAX_TEAMS       16
#define LW_START_ORDER_MAX_PER_TEAM    32


/*
 * Java:
 *   private final List<List<Entity>> teams = new ArrayList<...>();
 *   private int totalEntities = 0;
 */
typedef struct {
    struct LwEntity *teams[LW_START_ORDER_MAX_TEAMS][LW_START_ORDER_MAX_PER_TEAM];
    int              n_per_team[LW_START_ORDER_MAX_TEAMS];
    int              n_teams;
    int              total_entities;
} LwStartOrder;


/* (Implicit Java default ctor.) */
void lw_start_order_init(LwStartOrder *self);

/* public void addEntity(Entity entity) */
void lw_start_order_add_entity(LwStartOrder *self, struct LwEntity *entity);

/* public List<Entity> compute(State state)
 *
 * Writes the ordered entity list into out_buf (caller-owned). Returns
 * the number of entities written (== self->total_entities). */
int lw_start_order_compute(LwStartOrder *self, struct LwState *state,
                           struct LwEntity **out_buf, int out_cap);


#endif /* LW_START_ORDER_H */
