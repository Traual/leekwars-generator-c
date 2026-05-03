/*
 * lw_leek.c -- 1:1 port of leek/Leek.java
 *
 * Leek is a thin Entity subclass whose only job in Java is to set the
 * type tag and override a couple of virtual methods. In C, the type-tag
 * is a field on LwEntity, so the leek init functions just delegate to
 * the matching entity init function and stamp `type = LW_ENTITY_TYPE_LEEK`.
 *
 * Reference: java_reference/src/main/java/com/leekwars/generator/leek/Leek.java
 */
#include "lw_leek.h"


/* Java: public Leek() {} -- inherits the no-arg Entity ctor. */
void lw_leek_init_default(LwEntity *self) {
    lw_entity_init_default(self);
    self->type = LW_ENTITY_TYPE_LEEK;
}


/* Java: public Leek(Integer id, String name) { super(id, name); } */
void lw_leek_init_id_name(LwEntity *self, int id, const char *name) {
    lw_entity_init_id_name(self, id, name);
    self->type = LW_ENTITY_TYPE_LEEK;
}


/* Java: public Leek(Integer id, String name, int farmer, ...) {
 *           super(id, name, farmer, level, ...);
 *       }
 */
void lw_leek_init_full(LwEntity *self, int id, const char *name, int farmer,
                       int level, int life, int turn_point, int move_point,
                       int force, int agility, int frequency, int wisdom,
                       int resistance, int science, int magic, int cores, int ram,
                       int skin, int metal, int face, int team_id,
                       const char *team_name, int ai_id, const char *ai_name,
                       const char *farmer_name, const char *farmer_country, int hat) {
    lw_entity_init_full(self, id, name, farmer, level, life, turn_point, move_point,
                        force, agility, frequency, wisdom, resistance, science, magic,
                        cores, ram, skin, metal, face, team_id, team_name, ai_id,
                        ai_name, farmer_name, farmer_country, hat);
    self->type = LW_ENTITY_TYPE_LEEK;
}


/* Java: public Leek(Leek leek) { super(leek); } */
void lw_leek_init_copy(LwEntity *self, const LwEntity *leek) {
    lw_entity_init_copy(self, leek);
    self->type = LW_ENTITY_TYPE_LEEK;
}
