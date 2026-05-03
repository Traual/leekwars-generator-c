/*
 * lw_bulb_template.c -- 1:1 port of bulbs/BulbTemplate.java + bulbs/Bulbs.java
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/bulbs/BulbTemplate.java
 *   java_reference/src/main/java/com/leekwars/generator/bulbs/Bulbs.java
 *   java_reference/src/main/java/com/leekwars/generator/entity/Bulb.java
 */
#include "lw_bulb_template.h"
#include "lw_util.h"
#include "lw_constants.h"

#include <math.h>
#include <string.h>


/* ---- Internal string helper -------------------------------------- */

static void lw_bt_str_copy_n(char *dst, const char *src, size_t cap) {
    if (cap == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < cap && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}


/* ---- BulbTemplate construction ----------------------------------- */

void lw_bulb_template_init(LwBulbTemplate *self, int id, const char *name,
                           int min_life, int max_life,
                           int min_strength, int max_strength,
                           int min_wisdom, int max_wisdom,
                           int min_agility, int max_agility,
                           int min_resistance, int max_resistance,
                           int min_science, int max_science,
                           int min_magic, int max_magic,
                           int min_tp, int max_tp,
                           int min_mp, int max_mp) {
    memset(self, 0, sizeof(*self));

    self->m_id = id;
    lw_bt_str_copy_n(self->m_name, name, sizeof(self->m_name));

    self->m_min_life = min_life;       self->m_max_life = max_life;
    self->m_min_strength = min_strength;     self->m_max_strength = max_strength;
    self->m_min_wisdom = min_wisdom;        self->m_max_wisdom = max_wisdom;
    self->m_min_agility = min_agility;        self->m_max_agility = max_agility;
    self->m_min_resistance = min_resistance;     self->m_max_resistance = max_resistance;
    self->m_min_science = min_science;        self->m_max_science = max_science;
    self->m_min_magic = min_magic;          self->m_max_magic = max_magic;
    self->m_min_tp = min_tp;             self->m_max_tp = max_tp;
    self->m_min_mp = min_mp;             self->m_max_mp = max_mp;
}

void lw_bulb_template_add_chip(LwBulbTemplate *self, struct LwChip *chip) {
    if (chip == NULL) return;
    if (self->n_chips >= LW_BULB_TEMPLATE_MAX_CHIPS) return;
    self->m_chips[self->n_chips++] = chip;
}


/* ---- Trivial accessors ------------------------------------------- */

int          lw_bulb_template_get_id  (const LwBulbTemplate *self) { return self->m_id; }
const char*  lw_bulb_template_get_name(const LwBulbTemplate *self) { return self->m_name; }

int lw_bulb_template_get_min_life      (const LwBulbTemplate *self) { return self->m_min_life; }
int lw_bulb_template_get_max_life      (const LwBulbTemplate *self) { return self->m_max_life; }
int lw_bulb_template_get_min_strength  (const LwBulbTemplate *self) { return self->m_min_strength; }
int lw_bulb_template_get_max_strength  (const LwBulbTemplate *self) { return self->m_max_strength; }
int lw_bulb_template_get_min_wisdom    (const LwBulbTemplate *self) { return self->m_min_wisdom; }
int lw_bulb_template_get_max_wisdom    (const LwBulbTemplate *self) { return self->m_max_wisdom; }
int lw_bulb_template_get_min_agility   (const LwBulbTemplate *self) { return self->m_min_agility; }
int lw_bulb_template_get_max_agility   (const LwBulbTemplate *self) { return self->m_max_agility; }
int lw_bulb_template_get_min_resistance(const LwBulbTemplate *self) { return self->m_min_resistance; }
int lw_bulb_template_get_max_resistance(const LwBulbTemplate *self) { return self->m_max_resistance; }
int lw_bulb_template_get_min_science   (const LwBulbTemplate *self) { return self->m_min_science; }
int lw_bulb_template_get_max_science   (const LwBulbTemplate *self) { return self->m_max_science; }
int lw_bulb_template_get_min_magic     (const LwBulbTemplate *self) { return self->m_min_magic; }
int lw_bulb_template_get_max_magic     (const LwBulbTemplate *self) { return self->m_max_magic; }
int lw_bulb_template_get_min_tp        (const LwBulbTemplate *self) { return self->m_min_tp; }
int lw_bulb_template_get_max_tp        (const LwBulbTemplate *self) { return self->m_max_tp; }
int lw_bulb_template_get_min_mp        (const LwBulbTemplate *self) { return self->m_min_mp; }
int lw_bulb_template_get_max_mp        (const LwBulbTemplate *self) { return self->m_max_mp; }

int lw_bulb_template_get_chips_count(const LwBulbTemplate *self) { return self->n_chips; }
struct LwChip* lw_bulb_template_get_chip_at(const LwBulbTemplate *self, int index) {
    if (index < 0 || index >= self->n_chips) return NULL;
    return self->m_chips[index];
}


/* Java: public static int base(int base, int bonus, double coeff, double multiplier) {
 *     return (int) ((base + Math.floor((bonus - base) * coeff)) * multiplier);
 * }
 */
int lw_bulb_template_base(int base, int bonus, double coeff, double multiplier) {
    double inner = (double)base + floor(((double)(bonus - base)) * coeff);
    return (int)(inner * multiplier);
}


/* Java: public Bulb createInvocation(Entity owner, int id, int level, boolean critical) {
 *           double c = Math.min(300d, owner.getLevel()) / (300d);
 *           double multiplier = critical ? 1.2 : 1.0;
 *
 *           Bulb inv = new Bulb(owner, id, mName, level,
 *                   base(mMinLife, mMaxLife, c, multiplier),
 *                   ... etc ...
 *                   1, 6,
 *                   base(mMinTp, mMaxTp, c, multiplier),
 *                   base(mMinMp, mMaxMp, c, multiplier),
 *                   mId, 0);
 *
 *           for (Chip chip : mChips) {
 *               inv.addChip(chip);
 *           }
 *
 *           return inv;
 *       }
 *
 * Bulb's ctor (entity/Bulb.java line 13) maps these positional args to:
 *     super(id, name, owner.getFarmer(), level,
 *           life, tp, mp,
 *           strength, agility, /\* frequency = 0 *\/ 0, wisdom,
 *           resistance, science, magic, cores, ram,
 *           skin, /\* metal = false *\/, /\* face = 0 *\/,
 *           owner.getTeamId(), owner.getTeamName(),
 *           owner.getAIId(), owner.getAIName(),
 *           owner.getFarmerName(), owner.getFarmerCountry(),
 *           hat);
 *     setCompositionName(owner.getCompositionName());
 *     mOwner = owner;
 *     state = mOwner.state;
 */
int lw_bulb_template_create_invocation(const LwBulbTemplate *self,
                                       LwEntity *out_entity,
                                       LwEntity *owner, int id,
                                       int level, int critical) {

    double c = ((double)lw_min_int(300, lw_entity_get_level(owner))) / 300.0;
    double multiplier = critical ? 1.2 : 1.0;

    int life       = lw_bulb_template_base(self->m_min_life,       self->m_max_life,       c, multiplier);
    int strength   = lw_bulb_template_base(self->m_min_strength,   self->m_max_strength,   c, multiplier);
    int wisdom     = lw_bulb_template_base(self->m_min_wisdom,     self->m_max_wisdom,     c, multiplier);
    int agility    = lw_bulb_template_base(self->m_min_agility,    self->m_max_agility,    c, multiplier);
    int resistance = lw_bulb_template_base(self->m_min_resistance, self->m_max_resistance, c, multiplier);
    int science    = lw_bulb_template_base(self->m_min_science,    self->m_max_science,    c, multiplier);
    int magic      = lw_bulb_template_base(self->m_min_magic,      self->m_max_magic,      c, multiplier);
    int tp         = lw_bulb_template_base(self->m_min_tp,         self->m_max_tp,         c, multiplier);
    int mp         = lw_bulb_template_base(self->m_min_mp,         self->m_max_mp,         c, multiplier);

    /* Bulb ctor positional args:
     *   skin = mId (Java: "mId, 0" -- so skin=mId, hat=0)
     *   hat  = 0
     *   metal = false
     *   face  = 0
     *   frequency = 0
     *   cores = 1, ram = 6
     */
    lw_entity_init_full(out_entity, id, self->m_name, lw_entity_get_farmer(owner),
                        level, life, tp, mp, strength, agility, /*frequency=*/0, wisdom,
                        resistance, science, magic, /*cores=*/1, /*ram=*/6,
                        /*skin=*/self->m_id, /*metal=*/0, /*face=*/0,
                        lw_entity_get_team_id(owner), lw_entity_get_team_name(owner),
                        lw_entity_get_ai_id(owner), lw_entity_get_ai_name(owner),
                        lw_entity_get_farmer_name(owner),
                        lw_entity_get_farmer_country(owner),
                        /*hat=*/0);

    /* Tag as bulb. */
    out_entity->type = LW_ENTITY_TYPE_BULB;

    /* setCompositionName(owner.getCompositionName()); */
    lw_entity_set_composition_name(out_entity, lw_entity_get_composition_name(owner));

    /* mOwner = owner; */
    out_entity->m_owner = owner;

    /* state = mOwner.state; */
    out_entity->state = owner->state;

    /* Add chips. */
    for (int i = 0; i < self->n_chips; i++) {
        lw_entity_add_chip(out_entity, self->m_chips[i]);
    }

    return 1;
}


/* ---- Bulbs registry ---------------------------------------------- *
 *
 * Java: private static TreeMap<Integer, BulbTemplate> sTemplates = new TreeMap<>();
 *
 * Linear scan over a small, sorted array; bulb template ids are sparse
 * so we just keep them in the order they're registered (the only
 * iteration happens through getInvocationTemplate which is keyed). */

static LwBulbTemplate* g_bulbs_registry[LW_BULB_TEMPLATE_REGISTRY_MAX];
static int             g_bulbs_registry_n = 0;


/* Java: public static BulbTemplate getInvocationTemplate(int id) { return sTemplates.get(id); } */
LwBulbTemplate* lw_bulbs_get_invocation_template(int id) {
    for (int i = 0; i < g_bulbs_registry_n; i++) {
        if (lw_bulb_template_get_id(g_bulbs_registry[i]) == id) {
            return g_bulbs_registry[i];
        }
    }
    return NULL;
}

/* Java: public static void addInvocationTemplate(BulbTemplate invocation) {
 *     sTemplates.put(invocation.getId(), invocation);
 * }
 *
 * Replace if id already present (TreeMap.put semantics).
 */
void lw_bulbs_add_invocation_template(LwBulbTemplate *invocation) {
    int id = lw_bulb_template_get_id(invocation);
    for (int i = 0; i < g_bulbs_registry_n; i++) {
        if (lw_bulb_template_get_id(g_bulbs_registry[i]) == id) {
            g_bulbs_registry[i] = invocation;
            return;
        }
    }
    if (g_bulbs_registry_n >= LW_BULB_TEMPLATE_REGISTRY_MAX) return;
    g_bulbs_registry[g_bulbs_registry_n++] = invocation;
}


void lw_bulbs_clear(void) {
    for (int i = 0; i < g_bulbs_registry_n; i++) {
        g_bulbs_registry[i] = NULL;
    }
    g_bulbs_registry_n = 0;
}
