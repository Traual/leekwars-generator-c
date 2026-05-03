/*
 * lw_leek.h -- 1:1 port of leek/Leek.java
 *
 * In Java, Leek extends Entity and adds nothing new beyond overriding
 * getType()/getLeek()/isSummon(). In C we don't subclass; instead we
 * provide Leek-specific init helpers that wrap lw_entity_init_* and set
 * `type = LW_ENTITY_TYPE_LEEK`.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/leek/Leek.java
 */
#ifndef LW_LEEK_H
#define LW_LEEK_H

#include "lw_entity.h"


/* Java: public Leek() {} */
void lw_leek_init_default(LwEntity *self);

/* Java: public Leek(Integer id, String name) { super(id, name); } */
void lw_leek_init_id_name(LwEntity *self, int id, const char *name);

/* Java: public Leek(Integer id, String name, int farmer, int level, ...) -- full ctor. */
void lw_leek_init_full(LwEntity *self, int id, const char *name, int farmer,
                       int level, int life, int turn_point, int move_point,
                       int force, int agility, int frequency, int wisdom,
                       int resistance, int science, int magic, int cores, int ram,
                       int skin, int metal, int face, int team_id,
                       const char *team_name, int ai_id, const char *ai_name,
                       const char *farmer_name, const char *farmer_country, int hat);

/* Java: public Leek(Leek leek) { super(leek); } */
void lw_leek_init_copy(LwEntity *self, const LwEntity *leek);


#endif /* LW_LEEK_H */
