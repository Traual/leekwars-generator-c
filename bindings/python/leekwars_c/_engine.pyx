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
        pass


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
                   list weapons, list chips):
        cdef int idx = self._s.n_entities
        if idx >= 90:
            raise OverflowError("entity slots full")
        cdef LwEntity *e = &self._s.entities[idx]
        memset(e, 0, sizeof(LwEntity))
        e.id = idx
        e.fid = fid
        e.team_id = team_id
        e.cell_id = cell_id
        e.hp = hp
        e.total_hp = total_hp
        e.base_stats[C_STAT_TP] = total_tp
        e.base_stats[C_STAT_MP] = total_mp
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
