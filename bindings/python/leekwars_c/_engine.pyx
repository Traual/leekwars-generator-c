# cython: language_level=3, boundscheck=False, wraparound=False, initializedcheck=False, cdivision=True
"""Cython bindings for the C engine.

Wraps the LwState struct as a Python-callable ``State`` class. Clone
is exposed as a method that does an internal memcpy (no Python-level
allocation per copy), legal_actions returns a list of Action wrappers,
and apply_action mutates State in place.

Topology setup (loading a real Leek Wars map) is done by the higher-
level Python layer and pushed in via ``set_topology`` -- the C engine
itself doesn't load scenarios.
"""

from libc.stdlib cimport calloc, free
from libc.string cimport memcpy, memset


# --- C declarations from the engine ----------------------------------
# Hard-coded sizes mirror lw_types.h. We don't ``cdef extern`` them
# because exposing the same identifier on the Python side would clash
# in the generated C.

DEF C_MAX_CELLS     = 700
DEF C_MAX_ENTITIES  = 90
DEF C_MAX_INVENTORY = 6
DEF C_MAX_PATH_LEN  = 32
DEF C_STAT_TP       = 1
DEF C_STAT_MP       = 2
DEF C_COORD_DIM     = 64
DEF C_STAT_COUNT    = 18


cdef extern from "lw_cell.h":
    ctypedef struct LwCell:
        int id
        int x
        int y
        unsigned char walkable
        unsigned char obstacle_size
        unsigned char composante


cdef extern from "lw_map.h":
    ctypedef struct LwTopology:
        int id, width, height, n_cells
        int min_x, max_x, min_y, max_y
        LwCell cells[C_MAX_CELLS]
        int coord_lut[C_COORD_DIM][C_COORD_DIM]
        int neighbors[C_MAX_CELLS][4]

    ctypedef struct LwMap:
        const LwTopology *topo
        int entity_at_cell[C_MAX_CELLS]


cdef extern from "lw_effect.h":
    ctypedef struct LwEffect:
        int id
        int turns
        double value1, value2
        double aoe
        int critical
        double critical_power
        double jet
        double erosion_rate
        int value
        int previous_total
        int target_count
        int propagate
        int modifiers
        int log_id
        int target_id
        int caster_id
        int attack_id
        int stats[C_STAT_COUNT]


cdef extern from "lw_entity.h":
    ctypedef struct LwEntity:
        int id, fid, team_id, farmer_id, level
        int hp, total_hp
        int used_tp, used_mp
        int cell_id
        int weapons[C_MAX_INVENTORY]
        int n_weapons
        int chips[C_MAX_INVENTORY]
        int n_chips
        int equipped_weapon
        int chip_cooldown[C_MAX_INVENTORY]
        int base_stats[C_STAT_COUNT]
        int buff_stats[C_STAT_COUNT]
        LwEffect effects[16]
        int n_effects
        unsigned int state_flags
        unsigned char alive


cdef extern from "lw_state.h":
    ctypedef struct LwState:
        LwMap map
        LwEntity entities[C_MAX_ENTITIES]
        int n_entities
        int entity_id_by_fid[C_MAX_ENTITIES]
        int initial_order[C_MAX_ENTITIES]
        int n_in_order
        int order_index
        int turn, max_turns, winner
        unsigned long long rng_n
        unsigned char scenario_type
        unsigned char context
        int seed

    LwState *lw_state_alloc()
    void     lw_state_free(LwState *s)
    void     lw_state_clone(LwState *dst, const LwState *src)


cdef extern from "lw_attack.h":
    ctypedef struct LwAttack:
        int min_range
        int max_range
        int launch_type
        int area
        int needs_los
        double value1, value2
        int n_effects
        int effect_id
        double effect_v1, effect_v2
        int effect_turns


cdef extern from "lw_action.h":
    ctypedef enum LwActionType:
        LW_ACTION_END        = 0
        LW_ACTION_SET_WEAPON = 1
        LW_ACTION_MOVE       = 2
        LW_ACTION_USE_WEAPON = 3
        LW_ACTION_USE_CHIP   = 4

    ctypedef struct LwAction:
        LwActionType type
        int weapon_id
        int chip_id
        int target_cell_id
        int path[C_MAX_PATH_LEN]
        int path_len

    int lw_apply_action(LwState *state, int entity_index, const LwAction *action)


cdef extern from "lw_legal.h":
    ctypedef struct LwInventoryProfile:
        int weapon_costs[C_MAX_INVENTORY]
        LwAttack weapon_attacks[C_MAX_INVENTORY]
        int chip_costs[C_MAX_INVENTORY]
        LwAttack chip_attacks[C_MAX_INVENTORY]

    int lw_legal_actions(const LwState *state,
                         int entity_index,
                         const LwInventoryProfile *profile,
                         LwAction *out_actions,
                         int max_out)


cdef extern from "lw_features.h":
    int LW_MLP_FEAT_DIM
    void lw_extract_mlp(const LwState *state, int my_team, float *out)


cdef extern from "lw_attack_apply.h":
    ctypedef struct LwAttackEffectSpec:
        int    type
        double value1, value2
        int    turns
        int    targets_filter
        int    modifiers

    ctypedef struct LwAttackSpec:
        int  attack_type
        int  item_id
        int  min_range, max_range
        int  launch_type
        int  area
        int  needs_los
        int  tp_cost
        int  n_effects
        LwAttackEffectSpec effects[8]

    int lw_apply_attack_full(LwState *state,
                             int caster_idx,
                             int target_cell_id,
                             const LwAttackSpec *attack)


cdef extern from "lw_catalog.h":
    int  lw_catalog_register(int item_id, const LwAttackSpec *spec)
    void lw_catalog_clear()
    int  lw_catalog_size()


cdef extern from "lw_order.h":
    void lw_compute_start_order(LwState *state)


cdef extern from "lw_turn.h":
    int lw_next_entity_turn(LwState *state)
    int lw_entity_start_turn(LwState *state, int entity_idx)
    int lw_entity_end_turn(LwState *state, int entity_idx)


cdef extern from "lw_winner.h":
    int LW_WIN_ONGOING
    int LW_WIN_DRAW
    int lw_compute_winner(const LwState *state, int draw_check_hp)


cdef extern from "lw_damage.h":
    int lw_apply_damage(LwState *state, int caster_idx, int target_idx,
                        double value1, double value2, double jet,
                        double aoe, double critical_power, int target_count)
    int lw_apply_damage_v2(LwState *state, int caster_idx, int target_idx,
                           double value1, double value2, double jet,
                           double aoe, double critical_power, int target_count,
                           double erosion_rate)
    int lw_apply_heal(LwState *state, int caster_idx, int target_idx,
                      double value1, double value2, double jet,
                      double aoe, double critical_power, int target_count)
    int lw_apply_absolute_shield(LwState *state, int caster_idx, int target_idx,
                                 double value1, double value2, double jet,
                                 double aoe, double critical_power)
    int lw_apply_relative_shield(LwState *state, int caster_idx, int target_idx,
                                 double value1, double value2, double jet,
                                 double aoe, double critical_power)
    int lw_apply_erosion(LwState *state, int target_idx,
                         int value, double rate)


cdef extern from "lw_effects.h":
    int lw_apply_buff_stat(LwState *state, int caster_idx, int target_idx,
                           int stat_index, int scale_stat,
                           double value1, double value2, double jet,
                           double aoe, double critical_power)
    int lw_apply_shackle(LwState *state, int caster_idx, int target_idx,
                         int stat_index, double value1, double value2,
                         double jet, double aoe, double critical_power)
    int lw_apply_aftereffect(LwState *state, int caster_idx, int target_idx,
                             double value1, double value2, double jet,
                             double aoe, double critical_power)
    int lw_compute_poison_damage(const LwState *state, int caster_idx,
                                  int target_idx, double value1, double value2,
                                  double jet, double aoe, double critical_power)
    int lw_apply_vitality(LwState *state, int caster_idx, int target_idx,
                          double value1, double value2, double jet,
                          double aoe, double critical_power)
    int lw_apply_nova_vitality(LwState *state, int caster_idx, int target_idx,
                               double value1, double value2, double jet,
                               double aoe, double critical_power)
    int lw_apply_nova_damage(LwState *state, int caster_idx, int target_idx,
                             double value1, double value2, double jet,
                             double aoe, double critical_power)
    int lw_apply_life_damage(LwState *state, int caster_idx, int target_idx,
                             double value1, double value2, double jet,
                             double aoe, double critical_power)
    int lw_apply_raw_buff_stat(LwState *state, int target_idx, int stat_index,
                               double value1, double value2, double jet,
                               double aoe, double critical_power)
    int lw_apply_vulnerability(LwState *state, int target_idx,
                               double value1, double value2, double jet,
                               double aoe, double critical_power)
    int lw_apply_absolute_vulnerability(LwState *state, int target_idx,
                                        double value1, double value2,
                                        double jet, double aoe,
                                        double critical_power)
    int lw_apply_raw_heal(LwState *state, int caster_idx, int target_idx,
                          double value1, double value2, double jet,
                          double aoe, double critical_power, int target_count)
    int lw_apply_steal_life(LwState *state, int target_idx, int previous_value)
    int lw_apply_steal_absolute_shield(LwState *state, int target_idx,
                                        int previous_value)
    int lw_apply_kill(LwState *state, int caster_idx, int target_idx)
    int lw_apply_add_state(LwState *state, int target_idx,
                           unsigned int state_flag)
    int lw_apply_remove_shackles(LwState *state, int target_idx)
    int lw_apply_antidote(LwState *state, int target_idx)
    int lw_apply_debuff(LwState *state, int caster_idx, int target_idx,
                        double value1, double value2, double jet,
                        double aoe, double critical_power, int target_count)
    int lw_apply_total_debuff(LwState *state, int caster_idx, int target_idx,
                              double value1, double value2, double jet,
                              double aoe, double critical_power,
                              int target_count)
    int lw_apply_multiply_stats(LwState *state, int caster_idx,
                                int target_idx, double value1)
    int lw_apply_resurrect(LwState *state, int target_idx, int dest_cell,
                           int full_life, int critical)

    int lw_tick_poison(LwState *state, int target_idx, int per_turn_damage)
    int lw_tick_aftereffect(LwState *state, int target_idx, int per_turn_damage)
    int lw_tick_heal(LwState *state, int target_idx, int per_turn_heal)


cdef extern from "lw_critical.h":
    int    lw_roll_critical(LwState *state, int caster_idx)
    double lw_roll_critical_power(LwState *state, int caster_idx)


cdef extern from "lw_movement.h":
    int lw_compute_push_dest(const LwState *state,
                              int entity_cell, int target_cell, int caster_cell)
    int lw_compute_attract_dest(const LwState *state,
                                 int entity_cell, int target_cell, int caster_cell)
    int lw_apply_slide(LwState *state, int entity_idx, int dest_cell)
    int lw_apply_teleport(LwState *state, int entity_idx, int dest_cell)
    int lw_apply_permutation(LwState *state, int caster_idx, int target_idx)


cdef extern from "lw_effect_dispatch.h":
    ctypedef struct LwEffectInput:
        int    type
        int    caster_idx
        int    target_idx
        double value1, value2
        double jet
        int    turns
        double aoe
        int    critical
        int    attack_id
        int    modifiers
        int    previous_value
        int    target_count

    int lw_effect_create(LwState *state, const LwEffectInput *p)


# --- Python-side constants -------------------------------------------

LW_MAX_INVENTORY = 6
LW_MAX_PATH_LEN  = 32
LW_MAX_CELLS     = 700
LW_MAX_ENTITIES  = 90


class ActionType:
    END        = 0
    SET_WEAPON = 1
    MOVE       = 2
    USE_WEAPON = 3
    USE_CHIP   = 4


# --- Action wrapper ---------------------------------------------------

cdef class Action:
    """Lightweight Python view of an LwAction. We don't keep a pointer
    into State memory because Action lifetime is tied to the user's
    Python code, not the C struct's. Each Action carries its own copy."""

    cdef LwAction _a

    def __cinit__(self, int type=0, int weapon_id=-1, int chip_id=-1,
                  int target_cell_id=-1, list path=None):
        self._a.type = <LwActionType>type
        self._a.weapon_id = weapon_id
        self._a.chip_id = chip_id
        self._a.target_cell_id = target_cell_id
        self._a.path_len = 0
        if path is not None:
            n = min(len(path), LW_MAX_PATH_LEN)
            for i in range(n):
                self._a.path[i] = <int>path[i]
            self._a.path_len = n

    @property
    def type(self):
        return <int>self._a.type

    @property
    def weapon_id(self):
        return self._a.weapon_id

    @property
    def chip_id(self):
        return self._a.chip_id

    @property
    def target_cell_id(self):
        return self._a.target_cell_id

    @property
    def path(self):
        return [self._a.path[i] for i in range(self._a.path_len)]

    @property
    def path_len(self):
        return self._a.path_len

    def to_dict(self):
        t = <int>self._a.type
        if t == 0:
            return {"type": "end"}
        if t == 1:
            return {"type": "set_weapon", "weapon_id": self._a.weapon_id}
        if t == 2:
            return {"type": "move",
                    "cell_id": self._a.target_cell_id,
                    "path_ids": [self._a.path[i] for i in range(self._a.path_len)]}
        if t == 3:
            return {"type": "use_weapon", "cell_id": self._a.target_cell_id}
        if t == 4:
            return {"type": "use_chip",
                    "chip_id": self._a.chip_id,
                    "cell_id": self._a.target_cell_id}
        return {"type": "?"}


cdef Action _action_from(LwAction a):
    cdef Action out = Action.__new__(Action)
    out._a = a
    return out


# --- Inventory profile ------------------------------------------------

cdef class InventoryProfile:
    """Holds the LwAttack descriptors for an entity's weapons + chips.
    The Python AI builds these once per entity per turn and reuses
    across multiple legal_actions calls."""

    cdef LwInventoryProfile _p

    def __cinit__(self):
        memset(&self._p, 0, sizeof(self._p))

    def set_weapon(self, int slot, int cost,
                   int min_range, int max_range,
                   int launch_type, int needs_los,
                   double value1=0.0, double value2=0.0):
        if slot < 0 or slot >= 6:
            raise IndexError("weapon slot out of range")
        self._p.weapon_costs[slot] = cost
        self._p.weapon_attacks[slot].min_range = min_range
        self._p.weapon_attacks[slot].max_range = max_range
        self._p.weapon_attacks[slot].launch_type = launch_type
        self._p.weapon_attacks[slot].needs_los = needs_los
        self._p.weapon_attacks[slot].area = 0
        self._p.weapon_attacks[slot].value1 = value1
        self._p.weapon_attacks[slot].value2 = value2

    def set_chip(self, int slot, int cost,
                 int min_range, int max_range,
                 int launch_type, int needs_los,
                 double value1=0.0, double value2=0.0):
        if slot < 0 or slot >= 6:
            raise IndexError("chip slot out of range")
        self._p.chip_costs[slot] = cost
        self._p.chip_attacks[slot].min_range = min_range
        self._p.chip_attacks[slot].max_range = max_range
        self._p.chip_attacks[slot].launch_type = launch_type
        self._p.chip_attacks[slot].needs_los = needs_los
        self._p.chip_attacks[slot].area = 0
        self._p.chip_attacks[slot].value1 = value1
        self._p.chip_attacks[slot].value2 = value2


# --- Attack spec + catalog API ---------------------------------------

cdef class AttackSpec:
    """Python view of an LwAttackSpec.

    An attack is a (range, area, launch type, LoS) profile plus a list
    of effects to apply on each target. Build one per weapon / chip and
    register it with :func:`catalog_register` so apply_action's
    USE_WEAPON / USE_CHIP branches can route through the byte-for-byte
    pipeline.
    """

    cdef LwAttackSpec _s

    def __cinit__(self, int item_id, int attack_type=1,
                  int min_range=1, int max_range=1,
                  int launch_type=1, int area=1,
                  int needs_los=1, int tp_cost=0):
        memset(&self._s, 0, sizeof(LwAttackSpec))
        self._s.attack_type = attack_type
        self._s.item_id = item_id
        self._s.min_range = min_range
        self._s.max_range = max_range
        self._s.launch_type = launch_type
        self._s.area = area
        self._s.needs_los = needs_los
        self._s.tp_cost = tp_cost
        self._s.n_effects = 0

    def add_effect(self, int type, double value1=0.0, double value2=0.0,
                   int turns=0, int targets_filter=0, int modifiers=0):
        """Append one EffectParameters slot. Up to 8 per attack."""
        if self._s.n_effects >= 8:
            raise OverflowError("attack effect slots full (max 8)")
        cdef int i = self._s.n_effects
        self._s.effects[i].type = type
        self._s.effects[i].value1 = value1
        self._s.effects[i].value2 = value2
        self._s.effects[i].turns = turns
        self._s.effects[i].targets_filter = targets_filter
        self._s.effects[i].modifiers = modifiers
        self._s.n_effects += 1
        return self

    @property
    def item_id(self):
        return self._s.item_id

    @property
    def n_effects(self):
        return self._s.n_effects


def catalog_register(AttackSpec spec):
    """Register an attack spec by item id. Subsequent USE_WEAPON /
    USE_CHIP actions referencing this item id will route through the
    byte-for-byte pipeline."""
    cdef int rc = lw_catalog_register(spec._s.item_id, &spec._s)
    if rc != 0:
        raise RuntimeError("catalog full or invalid spec")


def catalog_clear():
    """Wipe every registered attack spec. Mostly useful for tests."""
    lw_catalog_clear()


def catalog_size():
    """Return the number of items currently registered."""
    return lw_catalog_size()


# --- Winner sentinels exposed to Python ------------------------------

WIN_ONGOING = -1
WIN_DRAW    = -2


# --- State wrapper ----------------------------------------------------

cdef class State:
    """Wraps an LwState* with Python lifecycle. Clone uses memcpy."""

    cdef LwState *_s
    cdef bint     _own
    # Hold a reference to the topology data so Python doesn't GC it
    # while the C state has a pointer to it.
    cdef object   _topo_ref

    def __cinit__(self):
        self._s = lw_state_alloc()
        if self._s == NULL:
            raise MemoryError("lw_state_alloc failed")
        self._own = True
        self._topo_ref = None

    def __dealloc__(self):
        if self._own and self._s != NULL:
            lw_state_free(self._s)
            self._s = NULL

    # -- topology setup (called once per scenario) --------------------

    def set_topology(self, Topology topo):
        """Attach a topology. State keeps a reference so the topology
        memory stays alive."""
        self._topo_ref = topo
        self._s.map.topo = topo._t

    # -- entity management --------------------------------------------

    @property
    def n_entities(self):
        return self._s.n_entities

    def add_entity(self, int fid, int team_id, int cell_id,
                   int total_tp, int total_mp,
                   int hp, int total_hp,
                   list weapons, list chips,
                   int level=1,
                   int strength=0, int agility=0,
                   int wisdom=0, int resistance=0,
                   int science=0, int magic=0,
                   int frequency=100, int power=0,
                   int absolute_shield=0, int relative_shield=0,
                   int damage_return=0):
        """Add an entity to the state. All stats are set into base_stats;
        buff_stats start at 0 and accumulate buffs/debuffs as effects
        are applied during the fight."""
        cdef int idx = self._s.n_entities
        if idx >= 90:
            raise OverflowError("entity slots full")
        cdef LwEntity *e = &self._s.entities[idx]
        memset(e, 0, sizeof(LwEntity))
        e.id = idx
        e.fid = fid
        e.team_id = team_id
        e.cell_id = cell_id
        e.level = level
        e.hp = hp
        e.total_hp = total_hp

        # Base stats — indices must match include/lw_types.h
        # (Mismatch was the silent bug behind the C-backed AI's
        # divergent decisions before this refactor.)
        e.base_stats[0]  = total_hp        # LIFE
        e.base_stats[1]  = total_tp        # TP
        e.base_stats[2]  = total_mp        # MP
        e.base_stats[3]  = strength
        e.base_stats[4]  = agility
        e.base_stats[5]  = frequency
        e.base_stats[6]  = wisdom
        e.base_stats[9]  = absolute_shield
        e.base_stats[10] = relative_shield
        e.base_stats[11] = resistance
        e.base_stats[12] = science
        e.base_stats[13] = magic
        e.base_stats[14] = damage_return
        e.base_stats[15] = power

        e.alive = 1
        e.equipped_weapon = -1
        e.n_weapons = min(len(weapons), 6)
        for i in range(e.n_weapons):
            e.weapons[i] = <int>weapons[i]
        if e.n_weapons > 0:
            e.equipped_weapon = 0
        e.n_chips = min(len(chips), 6)
        for i in range(e.n_chips):
            e.chips[i] = <int>chips[i]
        if cell_id >= 0:
            self._s.map.entity_at_cell[cell_id] = idx
        self._s.entity_id_by_fid[idx] = fid
        self._s.n_entities += 1
        return idx

    # -- cloning ------------------------------------------------------

    def clone(self):
        cdef State new_state = State.__new__(State)
        new_state._s = lw_state_alloc()
        if new_state._s == NULL:
            raise MemoryError("clone alloc failed")
        new_state._own = True
        new_state._topo_ref = self._topo_ref
        lw_state_clone(new_state._s, self._s)
        return new_state

    # -- action API ---------------------------------------------------

    def legal_actions(self, int entity_index, InventoryProfile profile):
        cdef LwAction buf[1024]
        cdef int n = lw_legal_actions(self._s, entity_index, &profile._p, buf, 1024)
        return [_action_from(buf[i]) for i in range(n)]

    def apply_action(self, int entity_index, Action action):
        return lw_apply_action(self._s, entity_index, &action._a) != 0

    def apply_action_use_weapon(self, int entity_index, int weapon_id,
                                  int target_cell_id):
        """Convenience: build a USE_WEAPON action and apply it. Used by
        bench_fight.py to drive the fight loop without per-call Action()
        Python-object construction overhead (the per-call Action()
        constructor was ~2 us/call, dominating the fight time)."""
        cdef LwAction a
        a.type = LW_ACTION_USE_WEAPON
        a.weapon_id = weapon_id
        a.chip_id = -1
        a.target_cell_id = target_cell_id
        a.path_len = 0
        return lw_apply_action(self._s, entity_index, &a) != 0

    def apply_attack(self, int caster_idx, int target_cell_id,
                     AttackSpec spec):
        """Run the byte-for-byte attack pipeline directly with a spec
        (skips the catalog lookup). Consumes 2 RNG draws (critical +
        jet). Returns total damage dealt."""
        return lw_apply_attack_full(self._s, caster_idx, target_cell_id,
                                    &spec._s)

    # -- Direct apply hooks (used by the parity gate) ----------------
    # These call the byte-for-byte formulas with a caller-supplied jet
    # and critical_power, without rolling RNG. Useful for asserting
    # exact equivalence with the Python reference engine on the same
    # numeric inputs.

    def _apply_damage(self, int caster_idx, int target_idx,
                      double v1, double v2, double jet,
                      double aoe, double crit_pwr, int target_count):
        return lw_apply_damage(self._s, caster_idx, target_idx,
                               v1, v2, jet, aoe, crit_pwr, target_count)

    def _apply_damage_v2(self, int caster_idx, int target_idx,
                          double v1, double v2, double jet,
                          double aoe, double crit_pwr, int target_count,
                          double erosion_rate):
        """Full Python-parity damage path: applies erosion to target's
        total_hp via removeLife AND to caster's total_hp on the
        returnDamage branch."""
        return lw_apply_damage_v2(self._s, caster_idx, target_idx,
                                   v1, v2, jet, aoe, crit_pwr, target_count,
                                   erosion_rate)

    def _apply_heal(self, int caster_idx, int target_idx,
                    double v1, double v2, double jet,
                    double aoe, double crit_pwr, int target_count):
        return lw_apply_heal(self._s, caster_idx, target_idx,
                             v1, v2, jet, aoe, crit_pwr, target_count)

    def _apply_absolute_shield(self, int caster_idx, int target_idx,
                                double v1, double v2, double jet,
                                double aoe, double crit_pwr):
        return lw_apply_absolute_shield(self._s, caster_idx, target_idx,
                                         v1, v2, jet, aoe, crit_pwr)

    def _apply_relative_shield(self, int caster_idx, int target_idx,
                                double v1, double v2, double jet,
                                double aoe, double crit_pwr):
        return lw_apply_relative_shield(self._s, caster_idx, target_idx,
                                         v1, v2, jet, aoe, crit_pwr)

    def _apply_buff_stat(self, int caster_idx, int target_idx,
                          int stat_index, int scale_stat,
                          double v1, double v2, double jet,
                          double aoe, double crit_pwr):
        return lw_apply_buff_stat(self._s, caster_idx, target_idx,
                                   stat_index, scale_stat,
                                   v1, v2, jet, aoe, crit_pwr)

    def _apply_shackle(self, int caster_idx, int target_idx, int stat_index,
                        double v1, double v2, double jet,
                        double aoe, double crit_pwr):
        return lw_apply_shackle(self._s, caster_idx, target_idx, stat_index,
                                 v1, v2, jet, aoe, crit_pwr)

    def _apply_aftereffect(self, int caster_idx, int target_idx,
                            double v1, double v2, double jet,
                            double aoe, double crit_pwr):
        return lw_apply_aftereffect(self._s, caster_idx, target_idx,
                                     v1, v2, jet, aoe, crit_pwr)

    def _compute_poison_damage(self, int caster_idx, int target_idx,
                                double v1, double v2, double jet,
                                double aoe, double crit_pwr):
        return lw_compute_poison_damage(self._s, caster_idx, target_idx,
                                         v1, v2, jet, aoe, crit_pwr)

    def _apply_vitality(self, int caster_idx, int target_idx,
                         double v1, double v2, double jet,
                         double aoe, double crit_pwr):
        return lw_apply_vitality(self._s, caster_idx, target_idx,
                                  v1, v2, jet, aoe, crit_pwr)

    def _apply_nova_vitality(self, int caster_idx, int target_idx,
                              double v1, double v2, double jet,
                              double aoe, double crit_pwr):
        return lw_apply_nova_vitality(self._s, caster_idx, target_idx,
                                       v1, v2, jet, aoe, crit_pwr)

    def _apply_nova_damage(self, int caster_idx, int target_idx,
                            double v1, double v2, double jet,
                            double aoe, double crit_pwr):
        return lw_apply_nova_damage(self._s, caster_idx, target_idx,
                                     v1, v2, jet, aoe, crit_pwr)

    def _apply_life_damage(self, int caster_idx, int target_idx,
                            double v1, double v2, double jet,
                            double aoe, double crit_pwr):
        return lw_apply_life_damage(self._s, caster_idx, target_idx,
                                     v1, v2, jet, aoe, crit_pwr)

    def _apply_raw_buff_stat(self, int target_idx, int stat_index,
                              double v1, double v2, double jet,
                              double aoe, double crit_pwr):
        return lw_apply_raw_buff_stat(self._s, target_idx, stat_index,
                                       v1, v2, jet, aoe, crit_pwr)

    def _apply_vulnerability(self, int target_idx,
                              double v1, double v2, double jet,
                              double aoe, double crit_pwr):
        return lw_apply_vulnerability(self._s, target_idx,
                                       v1, v2, jet, aoe, crit_pwr)

    def _apply_absolute_vulnerability(self, int target_idx,
                                       double v1, double v2, double jet,
                                       double aoe, double crit_pwr):
        return lw_apply_absolute_vulnerability(self._s, target_idx,
                                                v1, v2, jet, aoe, crit_pwr)

    def _apply_raw_heal(self, int caster_idx, int target_idx,
                        double v1, double v2, double jet,
                        double aoe, double crit_pwr, int target_count):
        return lw_apply_raw_heal(self._s, caster_idx, target_idx,
                                  v1, v2, jet, aoe, crit_pwr, target_count)

    def _apply_steal_life(self, int target_idx, int previous_value):
        return lw_apply_steal_life(self._s, target_idx, previous_value)

    def _apply_steal_absolute_shield(self, int target_idx, int previous_value):
        return lw_apply_steal_absolute_shield(self._s, target_idx, previous_value)

    def _apply_kill(self, int caster_idx, int target_idx):
        return lw_apply_kill(self._s, caster_idx, target_idx)

    def _apply_add_state(self, int target_idx, unsigned int state_flag):
        return lw_apply_add_state(self._s, target_idx, state_flag)

    def _apply_remove_shackles(self, int target_idx):
        return lw_apply_remove_shackles(self._s, target_idx)

    def _apply_antidote(self, int target_idx):
        return lw_apply_antidote(self._s, target_idx)

    def _apply_debuff(self, int caster_idx, int target_idx,
                      double v1, double v2, double jet,
                      double aoe, double crit_pwr, int target_count):
        return lw_apply_debuff(self._s, caster_idx, target_idx,
                                v1, v2, jet, aoe, crit_pwr, target_count)

    def _apply_total_debuff(self, int caster_idx, int target_idx,
                             double v1, double v2, double jet,
                             double aoe, double crit_pwr, int target_count):
        return lw_apply_total_debuff(self._s, caster_idx, target_idx,
                                      v1, v2, jet, aoe, crit_pwr, target_count)

    def _apply_multiply_stats(self, int caster_idx, int target_idx,
                               double value1):
        return lw_apply_multiply_stats(self._s, caster_idx, target_idx, value1)

    def _apply_resurrect(self, int target_idx, int dest_cell,
                         int full_life, int critical):
        return lw_apply_resurrect(self._s, target_idx, dest_cell,
                                   full_life, critical)

    def _apply_erosion(self, int target_idx, int value, double rate):
        return lw_apply_erosion(self._s, target_idx, value, rate)

    def _tick_poison(self, int target_idx, int per_turn):
        return lw_tick_poison(self._s, target_idx, per_turn)

    def _tick_aftereffect(self, int target_idx, int per_turn):
        return lw_tick_aftereffect(self._s, target_idx, per_turn)

    def _tick_heal(self, int target_idx, int per_turn):
        return lw_tick_heal(self._s, target_idx, per_turn)

    def _set_rng(self, long long seed):
        """Reseed the LCG to match Python's _DefaultRandom.seed(seed).
        Java parity: signed int64 cast preserves the bit pattern."""
        self._s.rng_n = <unsigned long long>(<long long>seed)

    def _get_rng(self):
        return <long long>self._s.rng_n

    def _roll_critical(self, int caster_idx):
        return lw_roll_critical(self._s, caster_idx)

    def _compute_push_dest(self, int entity_cell, int target_cell, int caster_cell):
        return lw_compute_push_dest(self._s, entity_cell, target_cell, caster_cell)

    def _compute_attract_dest(self, int entity_cell, int target_cell, int caster_cell):
        return lw_compute_attract_dest(self._s, entity_cell, target_cell, caster_cell)

    def _apply_slide(self, int entity_idx, int dest_cell):
        return lw_apply_slide(self._s, entity_idx, dest_cell)

    def _apply_teleport_to(self, int entity_idx, int dest_cell):
        return lw_apply_teleport(self._s, entity_idx, dest_cell)

    def _apply_permutation(self, int caster_idx, int target_idx):
        return lw_apply_permutation(self._s, caster_idx, target_idx)

    def _effect_create(self, dict params):
        """Run lw_effect_create — the C equivalent of Python's
        Effect.createEffect. Used by parity tests for stacking +
        replacement coverage."""
        cdef LwEffectInput p
        p.type = <int>params.get("type", 0)
        p.caster_idx = <int>params.get("caster_idx", 0)
        p.target_idx = <int>params.get("target_idx", 0)
        p.value1 = <double>params.get("value1", 0.0)
        p.value2 = <double>params.get("value2", 0.0)
        p.jet = <double>params.get("jet", 0.0)
        p.turns = <int>params.get("turns", 0)
        p.aoe = <double>params.get("aoe", 1.0)
        p.critical = <int>params.get("critical", 0)
        p.attack_id = <int>params.get("attack_id", -1)
        p.modifiers = <int>params.get("modifiers", 0)
        p.previous_value = <int>params.get("previous_value", 0)
        p.target_count = <int>params.get("target_count", 1)
        return lw_effect_create(self._s, &p)

    def entity_n_effects_at(self, int idx):
        if idx < 0 or idx >= self._s.n_entities:
            return 0
        return self._s.entities[idx].n_effects

    def effect_id_at(self, int idx, int slot):
        if idx < 0 or idx >= self._s.n_entities:
            return 0
        if slot < 0 or slot >= self._s.entities[idx].n_effects:
            return 0
        return self._s.entities[idx].effects[slot].id

    def extract_mlp_features(self, int my_team, out):
        """Fill ``out`` (a 256-element float32 buffer, typically a numpy
        view) with the MLP feature vector. ``out`` must implement the
        Python buffer protocol with at least 256 floats writable.

        Returns ``out`` unchanged for chaining.
        """
        cdef float[::1] view = out
        if view.shape[0] < 256:
            raise ValueError("out buffer must hold at least 256 float32")
        lw_extract_mlp(self._s, my_team, &view[0])
        return out

    # -- read-only accessors used by AI feature extraction ------------

    def entity_hp(self, int idx):
        return self._s.entities[idx].hp

    def entity_total_hp(self, int idx):
        return self._s.entities[idx].total_hp

    def entity_team(self, int idx):
        return self._s.entities[idx].team_id

    def entity_cell(self, int idx):
        return self._s.entities[idx].cell_id

    def entity_alive(self, int idx):
        return bool(self._s.entities[idx].alive)

    def entity_used_tp(self, int idx):
        return self._s.entities[idx].used_tp

    def entity_used_mp(self, int idx):
        return self._s.entities[idx].used_mp

    def entity_n_effects(self, int idx):
        return self._s.entities[idx].n_effects

    def entity_state_flags(self, int idx):
        return self._s.entities[idx].state_flags

    def entity_base_stat(self, int idx, int stat_index):
        return self._s.entities[idx].base_stats[stat_index]

    def entity_buff_stat(self, int idx, int stat_index):
        return self._s.entities[idx].buff_stats[stat_index]

    # -- Direct setters (for tests / parity gate) --------------------

    def _set_entity_hp(self, int idx, int hp, int total_hp):
        self._s.entities[idx].hp = hp
        self._s.entities[idx].total_hp = total_hp

    def _set_entity_alive(self, int idx, bint alive):
        self._s.entities[idx].alive = 1 if alive else 0

    def _set_entity_state_flag(self, int idx, unsigned int flag):
        self._s.entities[idx].state_flags = flag

    def _set_base_stat(self, int idx, int stat_index, int value):
        self._s.entities[idx].base_stats[stat_index] = value

    def _set_buff_stat(self, int idx, int stat_index, int value):
        self._s.entities[idx].buff_stats[stat_index] = value

    def _add_effect(self, int target_idx, int effect_id, int turns,
                     int value, int modifiers, dict stats_delta=None):
        """Push an LwEffect entry on entity.effects[] for parity tests.
        ``stats_delta`` maps stat_index -> int delta and is recorded
        on the effect's stats[] field. Returns the slot index, -1 if full."""
        cdef int slot, key, n_eff, i, int_val
        if target_idx < 0 or target_idx >= self._s.n_entities:
            return -1
        n_eff = self._s.entities[target_idx].n_effects
        if n_eff >= 16:
            return -1
        slot = n_eff
        # Zero all fields manually (Cython can't take & through Python
        # property chain for memset).
        self._s.entities[target_idx].effects[slot].id = effect_id
        self._s.entities[target_idx].effects[slot].turns = turns
        self._s.entities[target_idx].effects[slot].value1 = 0.0
        self._s.entities[target_idx].effects[slot].value2 = 0.0
        self._s.entities[target_idx].effects[slot].aoe = 1.0
        self._s.entities[target_idx].effects[slot].critical = 0
        self._s.entities[target_idx].effects[slot].critical_power = 1.0
        self._s.entities[target_idx].effects[slot].jet = 0.0
        self._s.entities[target_idx].effects[slot].erosion_rate = 0.0
        self._s.entities[target_idx].effects[slot].value = value
        self._s.entities[target_idx].effects[slot].previous_total = 0
        self._s.entities[target_idx].effects[slot].target_count = 0
        self._s.entities[target_idx].effects[slot].propagate = 0
        self._s.entities[target_idx].effects[slot].modifiers = modifiers
        self._s.entities[target_idx].effects[slot].log_id = 0
        self._s.entities[target_idx].effects[slot].target_id = target_idx
        self._s.entities[target_idx].effects[slot].caster_id = -1
        self._s.entities[target_idx].effects[slot].attack_id = -1
        for i in range(18):
            self._s.entities[target_idx].effects[slot].stats[i] = 0
        if stats_delta is not None:
            for key, val in stats_delta.items():
                if 0 <= key < 18:
                    int_val = <int>val
                    self._s.entities[target_idx].effects[slot].stats[<int>key] = int_val
        self._s.entities[target_idx].n_effects = n_eff + 1
        return slot

    def _effect_stat_at(self, int target_idx, int slot, int stat_index):
        if target_idx < 0 or target_idx >= self._s.n_entities:
            return 0
        if slot < 0 or slot >= self._s.entities[target_idx].n_effects:
            return 0
        return self._s.entities[target_idx].effects[slot].stats[stat_index]

    def _effect_value_at(self, int target_idx, int slot):
        if target_idx < 0 or target_idx >= self._s.n_entities:
            return 0
        if slot < 0 or slot >= self._s.entities[target_idx].n_effects:
            return 0
        return self._s.entities[target_idx].effects[slot].value

    # -- RNG management ----------------------------------------------

    def set_seed(self, long long seed):
        """Seed the engine RNG. Java-LCG signed 64-bit cast applied."""
        self._s.rng_n = <unsigned long long><long long>seed
        self._s.seed = <int>seed

    @property
    def rng_state(self):
        return <unsigned long long>self._s.rng_n

    @property
    def turn(self):
        return self._s.turn

    @property
    def order_index(self):
        return self._s.order_index

    @property
    def n_in_order(self):
        return self._s.n_in_order

    @property
    def initial_order(self):
        return [self._s.initial_order[i] for i in range(self._s.n_in_order)]

    # -- order + turn driver ----------------------------------------

    def compute_start_order(self):
        """Fill initial_order based on team frequencies. Consumes one
        RNG draw per distinct team (matches Python StartOrder.compute)."""
        lw_compute_start_order(self._s)

    def next_entity_turn(self):
        """Advance to the next alive entity in initial_order. Returns
        the entity index (or -1 if no one is alive). Increments
        ``self.turn`` and resets used_tp / used_mp on wrap-around."""
        return lw_next_entity_turn(self._s)

    def entity_start_turn(self, int entity_idx):
        """Reset used_tp / used_mp on the entity then run start-of-turn
        effect ticks (poison / aftereffect / heal). Returns net damage."""
        return lw_entity_start_turn(self._s, entity_idx)

    def entity_end_turn(self, int entity_idx):
        """Decrement effect counters; remove expired (unwinds buff_stats).
        Returns the number of effects that expired."""
        return lw_entity_end_turn(self._s, entity_idx)

    # -- winner --------------------------------------------------------

    def compute_winner(self, bint draw_check_hp=False):
        """Returns the winning team_id, WIN_ONGOING (-1) if 2+ teams
        still alive (and no tiebreak requested), WIN_DRAW (-2) if no
        team alive or HP tiebreak ended in a tie."""
        return lw_compute_winner(self._s, 1 if draw_check_hp else 0)


# --- Topology wrapper -------------------------------------------------

cdef class Topology:
    """Owns an LwTopology buffer. Built once per scenario and shared
    by the original State and all its clones."""

    cdef LwTopology *_t

    def __cinit__(self):
        self._t = <LwTopology*>calloc(1, sizeof(LwTopology))
        if self._t == NULL:
            raise MemoryError("topology alloc failed")

    def __dealloc__(self):
        if self._t != NULL:
            free(self._t)
            self._t = NULL

    @staticmethod
    def from_grid(int width, int height,
                  list cell_data, list neighbors):
        """Build a topology from a list of cells and per-cell neighbor
        4-tuples. Each cell_data entry is (id, x, y, walkable). Each
        neighbors[i] is (south, west, north, east) cell ids (-1 if none).
        Used by the Python loader to push a scenario into C."""
        cdef Topology t = Topology()
        cdef int n = len(cell_data)
        if n > 700:
            raise OverflowError("too many cells (max 700)")
        t._t.id = 0
        t._t.width = width
        t._t.height = height
        t._t.n_cells = n
        # Bounds
        cdef int min_x = 0, max_x = 0, min_y = 0, max_y = 0
        for i in range(n):
            cid, cx, cy, walk = cell_data[i]
            if i == 0:
                min_x = cx; max_x = cx; min_y = cy; max_y = cy
            else:
                if cx < min_x: min_x = cx
                if cx > max_x: max_x = cx
                if cy < min_y: min_y = cy
                if cy > max_y: max_y = cy
        t._t.min_x = min_x
        t._t.max_x = max_x
        t._t.min_y = min_y
        t._t.max_y = max_y
        # Init coord LUT to -1
        for ix in range(64):
            for iy in range(64):
                t._t.coord_lut[ix][iy] = -1
        # Fill cells + LUT
        for i in range(n):
            cid, cx, cy, walk = cell_data[i]
            t._t.cells[cid].id = cid
            t._t.cells[cid].x = cx
            t._t.cells[cid].y = cy
            t._t.cells[cid].walkable = 1 if walk else 0
            t._t.cells[cid].obstacle_size = 0
            t._t.cells[cid].composante = 0
            t._t.coord_lut[cx - min_x][cy - min_y] = cid
        # Fill neighbors
        for i in range(n):
            cid = cell_data[i][0]
            s, w, north, e = neighbors[i]
            t._t.neighbors[cid][0] = s
            t._t.neighbors[cid][1] = w
            t._t.neighbors[cid][2] = north
            t._t.neighbors[cid][3] = e
        return t

    @property
    def n_cells(self):
        return self._t.n_cells

    @property
    def width(self):
        return self._t.width

    @property
    def height(self):
        return self._t.height
