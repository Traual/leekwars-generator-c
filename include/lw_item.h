/*
 * lw_item.h -- 1:1 port of items/Item.java (and the implicit
 * "ItemTemplate" template-id concept used as plain int across the
 * engine: there is no Java class named ItemTemplate, just a int field
 * on Item).
 *
 * Java: public class Item {
 *           protected final int id;
 *           protected final int cost;
 *           protected final Attack attack;
 *           protected final String name;
 *           protected final int template;
 *       }
 *
 * In C, Weapon and Chip "extend" Item by embedding LwItem as their
 * first field (so a `LwItem *` pointer can target either when the
 * caller doesn't care about the subclass).
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/items/Item.java
 */
#ifndef LW_ITEM_H
#define LW_ITEM_H

/* Forward decl -- Attack lives in lw_attack.h. */
struct LwAttack;


/* The Java code refers to a "template" int on Item; there is no
 * separate ItemTemplate class.  We expose a typedef alias to make
 * call sites self-documenting (template ids are looked up by the UI
 * layer to render the right sprite). */
typedef int LwItemTemplate;


typedef struct LwItem {
    int                  id;        /* protected final int id */
    int                  cost;      /* protected final int cost */
    struct LwAttack     *attack;    /* protected final Attack attack */
    const char          *name;      /* protected final String name */
    int                  template_; /* protected final int template (renamed; `template` is a C++ keyword) */
} LwItem;


/* public Item(int id, int cost, String name, int template, Attack attack) */
static inline void lw_item_init(LwItem *self, int id, int cost,
                                const char *name, int template_,
                                struct LwAttack *attack) {
    self->id = id;
    self->cost = cost;
    self->name = name;
    self->template_ = template_;
    self->attack = attack;
}


/* Trivial getters -- inlined to mirror Java's getId / getCost / etc. */
static inline int                  lw_item_get_template(const LwItem *self) { return self->template_; }
static inline int                  lw_item_get_id      (const LwItem *self) { return self->id; }
static inline int                  lw_item_get_cost    (const LwItem *self) { return self->cost; }
static inline struct LwAttack*     lw_item_get_attack  (const LwItem *self) { return self->attack; }
static inline const char*          lw_item_get_name    (const LwItem *self) { return self->name; }


#endif /* LW_ITEM_H */
