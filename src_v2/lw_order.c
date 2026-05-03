/*
 * lw_order.c -- 1:1 port of state/Order.java
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/state/Order.java
 */
#include "lw_order.h"

#include <stddef.h>


/* Forward decls -- defined elsewhere. */
struct LwEntity;
struct LwState;
int lw_entity_get_fid(const struct LwEntity *e);
struct LwEntity* lw_state_get_entity(struct LwState *state, int id);


/* Java:
 *   public Order() {
 *       this.leeks = new ArrayList<Entity>();
 *       this.position = 0;
 *   }
 */
void lw_order_init(LwOrder *self) {
    self->n_leeks  = 0;
    self->position = 0;
    self->turn     = 1;
}


/* Java:
 *   public Order(Order order, State state) {
 *       this.leeks = new ArrayList<Entity>();
 *       this.position = order.position;
 *       for (var entity : order.leeks) {
 *           this.leeks.add(state.getEntity(entity.getFId()));
 *       }
 *   }
 *
 * NOTE: Java's constructor leaves `turn` at the default 1 (it's not
 * copied from `order`).  We mirror that bug-for-bug.
 */
void lw_order_copy(LwOrder *self, const LwOrder *order, struct LwState *state) {
    self->n_leeks  = 0;
    self->position = order->position;
    self->turn     = 1;
    for (int i = 0; i < order->n_leeks; i++) {
        struct LwEntity *e = order->leeks[i];
        self->leeks[self->n_leeks++] = lw_state_get_entity(state, lw_entity_get_fid(e));
    }
}


/* Java: public void addEntity(Entity leek) { leeks.add(leek); } */
void lw_order_add_entity(LwOrder *self, struct LwEntity *leek) {
    if (self->n_leeks >= LW_ORDER_MAX_ENTITIES) return;
    self->leeks[self->n_leeks++] = leek;
}


/* Internal: indexOf(entity), -1 if not present. */
static int lw_order_index_of(const LwOrder *self, struct LwEntity *e) {
    for (int i = 0; i < self->n_leeks; i++) {
        if (self->leeks[i] == e) return i;
    }
    return -1;
}


/* Java:
 *   public void addSummon(Entity owner, Entity invoc) {
 *       if (!leeks.contains(owner)) {
 *           return;
 *       }
 *       leeks.add(leeks.indexOf(owner) + 1, invoc);
 *   }
 *
 * NOTE: Java's ArrayList.add(int, T) does NOT shift `position` -- it's
 * a plain insertion. Order tracks position outside.
 */
void lw_order_add_summon(LwOrder *self, struct LwEntity *owner, struct LwEntity *invoc) {
    int idx = lw_order_index_of(self, owner);
    if (idx == -1) return;
    int insert = idx + 1;
    if (self->n_leeks >= LW_ORDER_MAX_ENTITIES) return;
    for (int i = self->n_leeks; i > insert; i--) {
        self->leeks[i] = self->leeks[i - 1];
    }
    self->leeks[insert] = invoc;
    self->n_leeks++;
}


/* Java:
 *   public void addEntity(int index, Entity invoc) {
 *       leeks.add(index, invoc);
 *       if (index <= position) {
 *           position++;
 *       }
 *   }
 */
void lw_order_add_entity_at(LwOrder *self, int index, struct LwEntity *invoc) {
    if (self->n_leeks >= LW_ORDER_MAX_ENTITIES) return;
    if (index < 0) index = 0;
    if (index > self->n_leeks) index = self->n_leeks;
    for (int i = self->n_leeks; i > index; i--) {
        self->leeks[i] = self->leeks[i - 1];
    }
    self->leeks[index] = invoc;
    self->n_leeks++;
    if (index <= self->position) {
        self->position++;
    }
}


/* Java:
 *   public void removeEntity(Entity leek) {
 *       int index = leeks.indexOf(leek);
 *       if (index == -1) {
 *           return;
 *       }
 *       if (index <= position) {
 *           position--;
 *       }
 *       leeks.remove(index);
 *       if (position == -1) {
 *           position = leeks.size() - 1;
 *           turn--; // On décrémente le tour car on va le réincrémenter tout de suite derrière
 *       }
 *   }
 */
void lw_order_remove_entity(LwOrder *self, struct LwEntity *leek) {
    int index = lw_order_index_of(self, leek);
    if (index == -1) {
        return;
    }
    if (index <= self->position) {
        self->position--;
    }
    for (int i = index; i + 1 < self->n_leeks; i++) {
        self->leeks[i] = self->leeks[i + 1];
    }
    self->n_leeks--;
    if (self->position == -1) {
        self->position = self->n_leeks - 1;
        self->turn--; // On décrémente le tour car on va le réincrémenter tout de suite derrière
    }
}


/* Java:
 *   public Entity current() {
 *       if (position < 0 || leeks.size() <= position) {
 *           return null;
 *       }
 *       return leeks.get(position);
 *   }
 */
struct LwEntity* lw_order_current(const LwOrder *self) {
    if (self->position < 0 || self->n_leeks <= self->position) {
        return NULL;
    }
    return self->leeks[self->position];
}


/* public int getTurn() { return turn; } */
int lw_order_get_turn(const LwOrder *self) {
    return self->turn;
}


/* public int getEntityTurnOrder(Entity e) { return leeks.indexOf(e) + 1; } */
int lw_order_get_entity_turn_order(const LwOrder *self, struct LwEntity *e) {
    return lw_order_index_of(self, e) + 1;
}


/* Java:
 *   public boolean next() {
 *       position++;
 *       if (position >= leeks.size()) {
 *           turn++;
 *           position = position % leeks.size();
 *           return true;
 *       }
 *       return false;
 *   }
 */
int lw_order_next(LwOrder *self) {
    self->position++;
    if (self->position >= self->n_leeks) {
        self->turn++;
        if (self->n_leeks > 0) {
            self->position = self->position % self->n_leeks;
        } else {
            self->position = 0;
        }
        return 1;
    }
    return 0;
}


/* Java: public Entity getNextPlayer() { return leeks.get((position + 1) % leeks.size()); } */
struct LwEntity* lw_order_get_next_player(const LwOrder *self) {
    if (self->n_leeks == 0) return NULL;
    return self->leeks[(self->position + 1) % self->n_leeks];
}


/* Java:
 *   public Entity getNextPlayer(Entity entity) {
 *       int index = leeks.indexOf(entity);
 *       if (index == -1) return null;
 *       return leeks.get((index + 1) % leeks.size());
 *   }
 */
struct LwEntity* lw_order_get_next_player_of(const LwOrder *self, struct LwEntity *entity) {
    int index = lw_order_index_of(self, entity);
    if (index == -1) return NULL;
    if (self->n_leeks == 0) return NULL;
    return self->leeks[(index + 1) % self->n_leeks];
}


/* Java:
 *   public Entity getPreviousPlayer() {
 *       int p = position - 1;
 *       if (p < 0) p += leeks.size();
 *       return leeks.get(p);
 *   }
 */
struct LwEntity* lw_order_get_previous_player(const LwOrder *self) {
    if (self->n_leeks == 0) return NULL;
    int p = self->position - 1;
    if (p < 0) p += self->n_leeks;
    return self->leeks[p];
}


/* Java:
 *   public Entity getPreviousPlayer(Entity entity) {
 *       int index = leeks.indexOf(entity);
 *       if (index == -1) return null;
 *       int p = index - 1;
 *       if (p < 0) p += leeks.size();
 *       return leeks.get(p);
 *   }
 */
struct LwEntity* lw_order_get_previous_player_of(const LwOrder *self, struct LwEntity *entity) {
    int index = lw_order_index_of(self, entity);
    if (index == -1) return NULL;
    int p = index - 1;
    if (p < 0) p += self->n_leeks;
    return self->leeks[p];
}


/* public List<Entity> getEntities() { return leeks; } */
struct LwEntity** lw_order_get_entities(LwOrder *self, int *out_n) {
    if (out_n) *out_n = self->n_leeks;
    return self->leeks;
}


/* public int getPosition() { return position; } */
int lw_order_get_position(const LwOrder *self) {
    return self->position;
}
