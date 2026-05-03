/*
 * lw_order.h -- 1:1 port of state/Order.java
 *
 * Holds the rolling turn-order list of entities. Java uses
 * ArrayList<Entity>; we use a fixed-capacity pointer array because the
 * total entity count for a fight is bounded (LW_ORDER_MAX_ENTITIES).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/Order.java
 */
#ifndef LW_ORDER_H
#define LW_ORDER_H

/* Forward declarations -- struct LwEntity defined in lw_entity.h
 * (being ported in parallel). */
struct LwEntity;
struct LwState;

/* Upper bound on the number of entities the order list can carry --
 * 2 teams * (5 leeks + 8 summons) plus headroom. */
#define LW_ORDER_MAX_ENTITIES   64


/*
 * Java:
 *   private final List<Entity> leeks;
 *   private int position = 0;
 *   private int turn = 1;
 */
typedef struct {
    struct LwEntity *leeks[LW_ORDER_MAX_ENTITIES];
    int              n_leeks;
    int              position;
    int              turn;
} LwOrder;


/* public Order() */
void lw_order_init(LwOrder *self);

/* public Order(Order order, State state) */
void lw_order_copy(LwOrder *self, const LwOrder *order, struct LwState *state);

/* public void addEntity(Entity leek) */
void lw_order_add_entity(LwOrder *self, struct LwEntity *leek);

/* public void addSummon(Entity owner, Entity invoc) */
void lw_order_add_summon(LwOrder *self, struct LwEntity *owner, struct LwEntity *invoc);

/* public void addEntity(int index, Entity invoc) */
void lw_order_add_entity_at(LwOrder *self, int index, struct LwEntity *invoc);

/* public void removeEntity(Entity leek) */
void lw_order_remove_entity(LwOrder *self, struct LwEntity *leek);

/* public Entity current() */
struct LwEntity* lw_order_current(const LwOrder *self);

/* public int getTurn() */
int lw_order_get_turn(const LwOrder *self);

/* public int getEntityTurnOrder(Entity e) */
int lw_order_get_entity_turn_order(const LwOrder *self, struct LwEntity *e);

/* public boolean next() */
int lw_order_next(LwOrder *self);

/* public Entity getNextPlayer() */
struct LwEntity* lw_order_get_next_player(const LwOrder *self);

/* public Entity getNextPlayer(Entity entity) */
struct LwEntity* lw_order_get_next_player_of(const LwOrder *self, struct LwEntity *entity);

/* public Entity getPreviousPlayer() */
struct LwEntity* lw_order_get_previous_player(const LwOrder *self);

/* public Entity getPreviousPlayer(Entity entity) */
struct LwEntity* lw_order_get_previous_player_of(const LwOrder *self, struct LwEntity *entity);

/* public List<Entity> getEntities() */
struct LwEntity** lw_order_get_entities(LwOrder *self, int *out_n);

/* public int getPosition() */
int lw_order_get_position(const LwOrder *self);


#endif /* LW_ORDER_H */
