# cython: language_level=3, boundscheck=False, wraparound=False
"""
leekwars_c._engine -- Cython binding for the line-by-line Java port.

Minimal API focused on parity testing:

    eng = Engine()
    eng.add_weapon(item_id=37, name='pistol', cost=3, min_range=1, max_range=7,
                    launch_type=1, area_id=1, los=True, max_uses=-1,
                    effects=[(1, 15.0, 5.0, 0, 31, 0)])  # (type, v1, v2, turns, targets, modifiers)
    eng.add_farmer(1, 'A', 'fr')
    eng.add_farmer(2, 'B', 'fr')
    eng.add_team(1, 'T1')
    eng.add_team(2, 'T2')
    eng.add_entity(team=0, fid=1, name='A', cell=72, ..., weapons=[37])
    eng.add_entity(team=1, fid=2, name='B', cell=144, ..., weapons=[37])
    eng.set_seed(1)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144])
    eng.set_ai_dispatch(my_callback)   # called per-entity per-turn
    outcome = eng.run()
    stream = eng.stream_dump()         # list of dicts in Java JSON shape
"""

from libc.stdint cimport int64_t, uint64_t
from libc.stdlib cimport malloc, free, calloc
from libc.string cimport memset, strncpy


# ===================== C declarations ============================

cdef extern from "lw_constants.h":
    int LW_ENTITY_TYPE_LEEK
    int LW_FIGHT_TYPE_SOLO
    int LW_FIGHT_TYPE_FARMER
    int LW_FIGHT_TYPE_TEAM
    int LW_FIGHT_TYPE_BATTLE_ROYALE
    int LW_CONTEXT_TEST


cdef extern from "lw_action_stream.h":
    int LW_LOG_MAX_ARGS
    ctypedef struct LwActionLog:
        int type
        int v[8]
        int n_args
        int extra_offset
        int extra_len

    ctypedef struct LwActionStream:
        int          enabled
        int          n_entries
        LwActionLog  entries[2048]
        int          extra[8192]
        int          n_extra
        int          next_effect_log_id


cdef extern from "lw_actions.h":
    ctypedef struct LwActions:
        LwActionStream stream

    void lw_actions_init(LwActions *self)


cdef extern from "lw_state.h":
    ctypedef struct LwState:
        uint64_t              rng_n
        int                   n_teams
        int                   n_entities
        int                   m_state
        LwActions             actions
        int                   context
        int                   type
        int                   seed
        int                   draw_check_life


cdef extern from "lw_outcome.h":
    int LW_OUTCOME_MAX_LOGS

    ctypedef struct LwOutcomeLogEntry:
        int     key
        void   *value

    ctypedef struct LwOutcome:
        LwActions          fight
        LwOutcomeLogEntry  logs[64]
        int                n_logs
        int                winner
        int                duration
        void              *statistics
        int                exception_status
        char               exception_message[512]
        int64_t            analyze_time
        int64_t            compilation_time
        int64_t            execution_time

    void lw_outcome_init(LwOutcome *self)


cdef extern from "lw_fight.h":
    ctypedef int (*lw_fight_ai_dispatch_t)(void *fight, void *entity, void *ai_file,
                                            int turn, void *userdata)

    ctypedef struct LwFight:
        int                  m_winteam
        void                *generator
        int                  m_id
        int                  m_boss
        int                  m_start_farmer
        int                  max_turns
        int64_t              execution_time
        void                *listener
        LwState             *state
        lw_fight_ai_dispatch_t ai_dispatch
        void                *ai_dispatch_userdata


cdef extern from "lw_entity_info.h":
    int LW_NAME_MAX
    int LW_AI_PATH_MAX
    int LW_COUNTRY_MAX
    int LW_FARMER_NAME_MAX
    int LW_TEAM_NAME_MAX
    int LW_COMP_NAME_MAX
    int LW_ENTITY_INFO_MAX_WEAPONS
    int LW_ENTITY_INFO_MAX_CHIPS

    ctypedef struct LwFarmerInfo:
        int  id
        char name[64]
        char country[16]

    ctypedef struct LwTeamInfo:
        int  id
        char name[64]
        char composition_name[64]
        int  has_composition_name
        int  level
        char turret_ai_path[256]
        int  turret_ai_owner

    ctypedef struct LwEntityInfo:
        int  id
        char name[64]
        char ai[256]
        int  ai_folder
        char ai_path[256]
        int  ai_version
        int  ai_strict
        int  aiOwner
        int  type
        int  farmer
        int  team
        int  level
        int  dead
        int  life
        int  tp
        int  mp
        int  strength
        int  agility
        int  frequency
        int  wisdom
        int  resistance
        int  science
        int  magic
        int  cores
        int  ram
        int  chips[32]
        int  n_chips
        int  weapons[16]
        int  n_weapons
        int  cell
        int  has_cell
        int  skin
        int  hat
        int  metal
        int  face
        int  custom_class
        int  orientation
        void *ai_file

    void lw_entity_info_init(LwEntityInfo *self)


cdef extern from "lw_scenario.h":
    int LW_SCENARIO_MAX_FARMERS
    int LW_SCENARIO_MAX_TEAMS
    int LW_SCENARIO_MAX_ENTITIES_PER_TEAM
    int LW_SCENARIO_MAP_OBSTACLES

    ctypedef struct LwCustomMap:
        int present
        int id
        int width
        int height
        int n_obstacles
        int obstacles_cell[256]
        int obstacles_type[256]
        int team1[16]
        int n_team1
        int team2[16]
        int n_team2

    ctypedef struct LwScenario:
        int64_t       seed
        int           max_turns
        int           type
        int           context
        int           fight_id
        int           boss
        int           draw_check_life
        LwCustomMap   map
        # opaque internal arrays

    void lw_scenario_init(LwScenario *self)
    void lw_scenario_add_entity(LwScenario *self, int team_id, const LwEntityInfo *entity)
    void lw_scenario_add_farmer(LwScenario *self, const LwFarmerInfo *farmer)
    void lw_scenario_add_team  (LwScenario *self, const LwTeamInfo *team)


cdef extern from "lw_generator.h":
    ctypedef struct LwGenerator:
        int use_leekscript_cache
        lw_fight_ai_dispatch_t ai_dispatch
        void                  *ai_dispatch_userdata

    void lw_generator_init(LwGenerator *self)
    void lw_generator_set_ai_dispatch(LwGenerator *self,
                                      lw_fight_ai_dispatch_t fn,
                                      void *userdata)
    int  lw_generator_run_scenario(LwGenerator *self,
                                   LwScenario *scenario,
                                   void *listener,
                                   void *register_manager,
                                   void *statistics_manager,
                                   LwState *state,
                                   LwFight *fight,
                                   LwOutcome *outcome)


cdef extern from "lw_effect_params.h":
    ctypedef struct LwEffectParameters:
        int    id
        double value1
        double value2
        int    turns
        int    targets
        int    modifiers


cdef extern from "lw_attack.h":
    ctypedef struct LwAttack:
        pass


cdef extern from "lw_weapon.h":
    ctypedef struct LwWeapon:
        pass

    void lw_weapon_init(LwWeapon *self,
                         int id, int cost, int min_range, int max_range,
                         const LwEffectParameters *effects, int n_effects,
                         int launch_type, int area_id, int los,
                         int template_id, const char *name,
                         const LwEffectParameters *passive_effects, int n_passives,
                         int max_uses)
    void      lw_weapons_add_weapon(LwWeapon *weapon)
    LwWeapon *lw_weapons_get_weapon(int id)


cdef extern from "lw_chip.h":
    ctypedef struct LwChip:
        pass

    ctypedef int LwChipType

    void lw_chip_init(LwChip *self,
                       int id, int cost, int min_range, int max_range,
                       const LwEffectParameters *effects, int n_effects,
                       int launch_type, int area, int los,
                       int cooldown, int team_cooldown, int initial_cooldown,
                       int level, int template_, const char *name,
                       LwChipType chip_type, int max_uses)
    void    lw_chips_add_chip(LwChip *chip)
    LwChip *lw_chips_get_chip(int id)


cdef extern from "lw_entity.h":
    ctypedef struct LwEntity:
        pass


# Helpers exposed by lw_glue.c (avoids extern-of-static-inline issues).
cdef extern:
    int      lw_state_index_of_entity(const LwState *state, const LwEntity *e)
    LwEntity* lw_state_get_entity_at  (LwState *state, int idx)
    int      lw_state_n_entities      (const LwState *state)
    int      lw_glue_entity_alive    (const LwEntity *e)
    int      lw_glue_entity_hp       (const LwEntity *e)
    int      lw_glue_entity_fid      (const LwEntity *e)
    int      lw_glue_entity_team     (const LwEntity *e)
    int      lw_glue_entity_cell_id  (const LwEntity *e)
    int      lw_glue_entity_used_tp  (const LwEntity *e)
    int      lw_glue_entity_used_mp  (const LwEntity *e)
    void     lw_glue_entity_mark_has_ai(LwEntity *e)
    int      lw_glue_apply_use_weapon(LwState *state, int entity_idx,
                                       int weapon_id, int target_cell_id)
    int      lw_glue_apply_use_chip  (LwState *state, LwFight *fight,
                                       int entity_idx, int chip_id,
                                       int target_cell_id)
    int      lw_glue_move_toward_cell  (LwState *state, int entity_idx,
                                          int target_cell_id, int max_mp)
    int      lw_glue_cell_distance      (LwState *state, int cell_a, int cell_b)
    int      lw_glue_cell_distance2     (LwState *state, int cell_a, int cell_b)


# Generator AI dispatch hook
cdef extern from "lw_generator.h":
    void lw_generator_set_ai_dispatch(LwGenerator *self,
                                      lw_fight_ai_dispatch_t fn,
                                      void *userdata)


cdef extern from "lw_bulb_template.h":
    int LW_BULB_TEMPLATE_MAX_CHIPS
    ctypedef struct LwBulbTemplate:
        pass
    void lw_bulb_template_init(LwBulbTemplate *self, int id, const char *name,
                                int min_life, int max_life,
                                int min_strength, int max_strength,
                                int min_wisdom, int max_wisdom,
                                int min_agility, int max_agility,
                                int min_resistance, int max_resistance,
                                int min_science, int max_science,
                                int min_magic, int max_magic,
                                int min_tp, int max_tp,
                                int min_mp, int max_mp)
    void lw_bulb_template_add_chip(LwBulbTemplate *self, LwChip *chip)
    void lw_bulbs_add_invocation_template(LwBulbTemplate *invocation)


# ===================== Python-side wrappers ======================

# C trampoline that the Fight calls on each entity's turn. It dispatches
# to the Python callback installed via Engine.set_ai_callback.
cdef int _ai_trampoline(void *fight, void *entity, void *ai_file,
                         int turn, void *userdata) noexcept with gil:
    cdef Engine eng = <Engine>userdata
    cdef LwEntity *e = <LwEntity*>entity
    cdef int idx = lw_state_index_of_entity(&eng.state, e)
    if eng._ai_callback is None:
        return 0
    try:
        ret = eng._ai_callback(idx, turn)
        return int(ret) if ret is not None else 0
    except Exception:
        import traceback; traceback.print_exc()
        return -1


cdef class Engine:
    """Top-level container for a single fight."""
    cdef LwGenerator generator
    cdef LwScenario  scenario
    cdef LwOutcome   outcome
    cdef LwState     state
    cdef LwFight     fight
    cdef object      _ai_callback     # Python callable
    cdef list        _registered_weapons  # keep alive
    cdef list        _registered_chips    # keep alive

    def __cinit__(self):
        lw_generator_init(&self.generator)
        lw_scenario_init(&self.scenario)
        lw_outcome_init(&self.outcome)
        memset(&self.state, 0, sizeof(LwState))
        memset(&self.fight, 0, sizeof(LwFight))
        self._ai_callback = None
        self._registered_weapons = []
        self._registered_chips = []
        # Sensible defaults
        self.scenario.max_turns = 30
        self.scenario.type = LW_FIGHT_TYPE_SOLO
        self.scenario.context = LW_CONTEXT_TEST
        self.scenario.seed = 1
        # Wire the AI trampoline -- the actual Python callback is set
        # via set_ai_callback below.
        lw_generator_set_ai_dispatch(&self.generator, _ai_trampoline,
                                      <void*>self)

    def set_ai_callback(self, callback):
        """Register a Python function called once per entity per turn.

        Signature: callback(entity_idx, turn) -> int (operation count).
        Inside the callback, use eng.fire_weapon(entity_idx, weapon_id,
        cell) etc. to apply actions.
        """
        self._ai_callback = callback

    # ---- bootstrap registries ----
    def add_weapon(self, int item_id, str name, int cost,
                   int min_range, int max_range, int launch_type,
                   int area_id, bint los, int max_uses,
                   list effects, list passive_effects=None):
        """Register one weapon. effects = list of (type,v1,v2,turns,targets,modifiers)."""
        cdef LwWeapon* w = <LwWeapon*>calloc(1, sizeof(LwWeapon))
        cdef int n_eff = len(effects)
        cdef int n_pas = 0 if passive_effects is None else len(passive_effects)
        cdef LwEffectParameters* eff = <LwEffectParameters*>calloc(max(n_eff,1), sizeof(LwEffectParameters))
        cdef LwEffectParameters* pas = <LwEffectParameters*>calloc(max(n_pas,1), sizeof(LwEffectParameters))
        cdef bytes name_b = name.encode('utf-8')
        cdef int i
        cdef tuple e
        for i in range(n_eff):
            e = effects[i]
            eff[i].id        = e[0]
            eff[i].value1    = e[1]
            eff[i].value2    = e[2]
            eff[i].turns     = e[3]
            eff[i].targets   = e[4]
            eff[i].modifiers = e[5]
        if passive_effects is not None:
            for i in range(n_pas):
                e = passive_effects[i]
                pas[i].id        = e[0]
                pas[i].value1    = e[1]
                pas[i].value2    = e[2]
                pas[i].turns     = e[3]
                pas[i].targets   = e[4]
                pas[i].modifiers = e[5]
        lw_weapon_init(w, item_id, cost, min_range, max_range,
                        eff, n_eff, launch_type, area_id, 1 if los else 0,
                        0, name_b, pas, n_pas, max_uses)
        lw_weapons_add_weapon(w)
        self._registered_weapons.append((item_id, name))

    def add_chip(self, int item_id, str name, int cost,
                 int min_range, int max_range, int launch_type,
                 int area_id, bint los, int max_uses,
                 int cooldown, int initial_cooldown, bint team_cooldown,
                 int level, int chip_type,
                 list effects, int template_id=0):
        """Register one chip. effects = list of (type,v1,v2,turns,targets,modifiers).
        chip_type matches LwChipType enum (NONE=0, DAMAGE=1, HEAL=2,
        RETURN=3, PROTECTION=4, BOOST=5, POISON=6, SHACKLE=7, BULB=8,
        TACTIC=9) -- this is the JSON field "type".
        """
        cdef LwChip* c = <LwChip*>calloc(1, sizeof(LwChip))
        cdef int n_eff = len(effects)
        cdef LwEffectParameters* eff = <LwEffectParameters*>calloc(max(n_eff,1), sizeof(LwEffectParameters))
        cdef bytes name_b = name.encode('utf-8')
        cdef int i
        cdef tuple e
        for i in range(n_eff):
            e = effects[i]
            eff[i].id        = e[0]
            eff[i].value1    = e[1]
            eff[i].value2    = e[2]
            eff[i].turns     = e[3]
            eff[i].targets   = e[4]
            eff[i].modifiers = e[5]
        lw_chip_init(c, item_id, cost, min_range, max_range,
                      eff, n_eff, launch_type, area_id, 1 if los else 0,
                      cooldown, 1 if team_cooldown else 0, initial_cooldown,
                      level, template_id, name_b, chip_type, max_uses)
        lw_chips_add_chip(c)
        self._registered_chips.append((item_id, name))

    # ---- scenario setup ----
    def add_farmer(self, int id, str name, str country):
        cdef LwFarmerInfo f
        memset(&f, 0, sizeof(LwFarmerInfo))
        f.id = id
        cdef bytes nb = name.encode('utf-8')
        cdef bytes cb = country.encode('utf-8')
        strncpy(f.name, nb, 63)
        strncpy(f.country, cb, 15)
        lw_scenario_add_farmer(&self.scenario, &f)

    def add_team(self, int id, str name):
        cdef LwTeamInfo t
        memset(&t, 0, sizeof(LwTeamInfo))
        t.id = id
        cdef bytes nb = name.encode('utf-8')
        strncpy(t.name, nb, 63)
        lw_scenario_add_team(&self.scenario, &t)

    def add_entity(self, int team, int fid, str name, int level,
                   int life, int tp, int mp, int strength, int agility,
                   int frequency, int wisdom, int resistance, int science,
                   int magic, int cores, int ram,
                   int farmer, int team_id,
                   list weapons, list chips=None,
                   int cell=-1):
        cdef LwEntityInfo e
        lw_entity_info_init(&e)
        e.id = fid
        cdef bytes nb = name.encode('utf-8')
        strncpy(e.name, nb, 63)
        e.type = LW_ENTITY_TYPE_LEEK
        e.farmer = farmer
        e.team = team_id
        e.level = level
        e.life = life
        e.tp = tp
        e.mp = mp
        e.strength = strength
        e.agility = agility
        e.frequency = frequency
        e.wisdom = wisdom
        e.resistance = resistance
        e.science = science
        e.magic = magic
        e.cores = cores
        e.ram = ram
        if cell >= 0:
            e.cell = cell
            e.has_cell = 1
        cdef int i
        e.n_weapons = min(len(weapons), 16)
        for i in range(e.n_weapons):
            e.weapons[i] = weapons[i]
        if chips is not None:
            e.n_chips = min(len(chips), 32)
            for i in range(e.n_chips):
                e.chips[i] = chips[i]
        lw_scenario_add_entity(&self.scenario, team, &e)

    def add_summon_template(self, int id, str name,
                             tuple life, tuple strength, tuple wisdom,
                             tuple agility, tuple resistance, tuple science,
                             tuple magic, tuple tp, tuple mp,
                             list chip_ids):
        """Register a Bulb (summon) template. life=(min,max) etc.
        chip_ids = list of chip ids the bulb knows (must already be
        registered via add_chip).
        """
        cdef LwBulbTemplate *t = <LwBulbTemplate*>calloc(1, 4096)  # generous
        if t == NULL:
            raise MemoryError("template alloc")
        cdef bytes nb = name.encode('utf-8')
        lw_bulb_template_init(t, id, nb,
                               int(life[0]), int(life[1]),
                               int(strength[0]), int(strength[1]),
                               int(wisdom[0]), int(wisdom[1]),
                               int(agility[0]), int(agility[1]),
                               int(resistance[0]), int(resistance[1]),
                               int(science[0]), int(science[1]),
                               int(magic[0]), int(magic[1]),
                               int(tp[0]), int(tp[1]),
                               int(mp[0]), int(mp[1]))
        cdef int cid
        cdef LwChip *c
        for cid in chip_ids:
            c = lw_chips_get_chip(cid)
            if c != NULL:
                lw_bulb_template_add_chip(t, c)
        lw_bulbs_add_invocation_template(t)

    def set_seed(self, int seed):
        self.scenario.seed = seed

    def set_max_turns(self, int n):
        self.scenario.max_turns = n

    def set_type(self, int t):
        """Fight type: 0=SOLO, 1=FARMER, 2=TEAM, 3=BATTLE_ROYALE."""
        self.scenario.type = t

    def set_custom_map(self, dict obstacles=None, list team1=None, list team2=None,
                        int map_id=0, int width=18, int height=18):
        """Configure a custom map: obstacles dict {cell_id: type_id},
        team1/team2 cell lists. Use map_id != 0 to mean "respect entity
        initial_cell during placement". The Python upstream uses id=0
        with team1/team2 lists for parity tests."""
        cdef int i
        memset(&self.scenario.map, 0, sizeof(LwCustomMap))
        self.scenario.map.present = 1
        self.scenario.map.id = map_id
        self.scenario.map.width = width
        self.scenario.map.height = height
        if obstacles:
            items = list(obstacles.items())
            n = min(len(items), 256)
            self.scenario.map.n_obstacles = n
            for i in range(n):
                k, v = items[i]
                self.scenario.map.obstacles_cell[i] = int(k)
                self.scenario.map.obstacles_type[i] = int(v)
        if team1:
            n = min(len(team1), 16)
            self.scenario.map.n_team1 = n
            for i in range(n):
                self.scenario.map.team1[i] = int(team1[i])
        if team2:
            n = min(len(team2), 16)
            self.scenario.map.n_team2 = n
            for i in range(n):
                self.scenario.map.team2[i] = int(team2[i])

    # ---- run ----
    def run(self):
        """Run the scenario; returns winner team id (or -1)."""
        cdef int rc = lw_generator_run_scenario(
            &self.generator, &self.scenario,
            NULL, NULL, NULL,
            &self.state, &self.fight, &self.outcome)
        return rc

    # ---- low-level entity access (called from inside the AI callback) ----
    def n_entities(self):
        return self.state.n_entities

    cdef LwEntity* _entity_at(self, int idx) except NULL:
        cdef int n = lw_state_n_entities(&self.state)
        cdef LwEntity *e
        if idx < 0 or idx >= n:
            raise IndexError("entity_idx out of range")
        e = lw_state_get_entity_at(&self.state, idx)
        if e == NULL:
            raise RuntimeError("entity is NULL")
        return e

    def entity_alive(self, int idx):
        return bool(lw_glue_entity_alive(self._entity_at(idx)))
    def entity_hp(self, int idx):
        return lw_glue_entity_hp(self._entity_at(idx))
    def entity_fid(self, int idx):
        return lw_glue_entity_fid(self._entity_at(idx))
    def entity_team(self, int idx):
        return lw_glue_entity_team(self._entity_at(idx))
    def entity_cell(self, int idx):
        return lw_glue_entity_cell_id(self._entity_at(idx))
    def entity_used_tp(self, int idx):
        return lw_glue_entity_used_tp(self._entity_at(idx))
    def entity_used_mp(self, int idx):
        return lw_glue_entity_used_mp(self._entity_at(idx))

    def fire_weapon(self, int entity_idx, int weapon_id, int target_cell):
        """Apply a USE_WEAPON action. Returns Attack.USE_* result code:
            2 = USE_CRITICAL, 1 = USE_SUCCESS,
           -1 = INVALID_TARGET, -2 = NOT_ENOUGH_TP,
           -4 = INVALID_POSITION, etc.
        """
        return lw_glue_apply_use_weapon(&self.state, entity_idx,
                                         weapon_id, target_cell)

    def move_toward(self, int entity_idx, int target_cell, int max_mp=10):
        """Move the entity toward target_cell using up to max_mp MP.
        Returns the number of MP actually used (0 if no movement)."""
        return lw_glue_move_toward_cell(&self.state, entity_idx,
                                          target_cell, max_mp)

    def cell_distance(self, int cell_a, int cell_b):
        """Manhattan distance between two cell ids (Pathfinding.getCaseDistance)."""
        return lw_glue_cell_distance(&self.state, cell_a, cell_b)

    def cell_distance2(self, int cell_a, int cell_b):
        """Squared Euclidean distance between two cell ids
        (Map.getDistance2 -- used by fight_class.getNearestEnemy)."""
        return lw_glue_cell_distance2(&self.state, cell_a, cell_b)

    def use_chip(self, int entity_idx, int chip_id, int target_cell):
        """Apply a USE_CHIP action. Returns Attack.USE_* result code (same
        codes as fire_weapon). The chip must have been registered first
        via add_chip(...) AND attached to the entity via add_entity(chips=[...]).
        Summon-typed chips dispatch through Fight.summonEntity which sets
        up the bulb's AI / birthTurn / etc; other chips go through state.useChip.
        """
        return lw_glue_apply_use_chip(&self.state, &self.fight, entity_idx,
                                       chip_id, target_cell)

    # ---- read action stream ----
    def stream_dump(self):
        """Return the action stream as a list of dicts."""
        cdef list out = []
        cdef int n = self.outcome.fight.stream.n_entries
        cdef int i, j
        cdef LwActionLog *e
        for i in range(n):
            e = &self.outcome.fight.stream.entries[i]
            args = []
            for j in range(e.n_args):
                args.append(e.v[j])
            d = {'type': e.type, 'args': args}
            if e.extra_len > 0:
                extra = []
                for j in range(e.extra_len):
                    extra.append(self.outcome.fight.stream.extra[e.extra_offset + j])
                d['extra'] = extra
            out.append(d)
        return out

    @property
    def winner(self):
        return self.outcome.winner

    @property
    def duration(self):
        return self.outcome.duration

    @property
    def rng_state(self):
        return self.state.rng_n


def hello():
    """Sanity check that the module imports."""
    return 'leekwars_c ready'
