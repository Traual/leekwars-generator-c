"""STRICT byte-by-byte action-stream parity: Python upstream vs C engine.

Phase A.1 of the AI master plan.

Setup constraints to make the comparison deterministic:
  - Python upstream uses a custom_map with NO random obstacles
    (empty obstacles dict) and FIXED team1/team2 start cells
  - C engine uses the same fixed start cells and no obstacles
  - Both seeded with the same RNG seed
  - Same agent class on both sides ('always fire equipped weapon at enemy')
  - Agility = 0 on all entities to suppress crit-roll-driven RNG drift

What this tool does:
  1. Drives the SAME scripted scenario on Python upstream and on C engine
  2. Captures full action streams from both
  3. Normalizes each stream to a common (canonical_type, key_args) tuple format
  4. Diffs entry-by-entry, reports first divergence

Sortie attendue après Phase A complète :
  N=1000 fights -> 0 divergence
"""
from __future__ import annotations

import argparse
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Locate Python upstream
for _candidate in (
    os.environ.get("LEEKWARS_PY_DIR"),
    "C:/Users/aurel/Desktop/leekwars_generator_python",
    "/mnt/c/Users/aurel/Desktop/leekwars_generator_python",
):
    if _candidate and os.path.isdir(_candidate) and _candidate not in sys.path:
        sys.path.insert(0, _candidate)
        break


# ============== Python action ID constants (from action.py) =================
PY_ACT = {
    "START_FIGHT": 0, "END_FIGHT": 4, "PLAYER_DEAD": 5, "NEW_TURN": 6,
    "LEEK_TURN": 7, "END_TURN": 8, "SUMMON": 9, "MOVE_TO": 10,
    "KILL": 11, "USE_CHIP": 12, "SET_WEAPON": 13, "STACK_EFFECT": 14,
    "CHEST_OPENED": 15, "USE_WEAPON": 16,
    "LOST_PT": 100, "LOST_LIFE": 101, "LOST_PM": 102, "HEAL": 103,
    "VITALITY": 104, "RESURRECT": 105, "LOSE_STRENGTH": 106,
    "NOVA_DAMAGE": 107, "DAMAGE_RETURN": 108, "LIFE_DAMAGE": 109,
    "POISON_DAMAGE": 110, "AFTEREFFECT": 111, "NOVA_VITALITY": 112,
    "LAMA": 201, "SAY": 203, "SHOW_CELL": 205,
    "ADD_WEAPON_EFFECT": 301, "ADD_CHIP_EFFECT": 302,
    "REMOVE_EFFECT": 303, "UPDATE_EFFECT": 304,
    "REDUCE_EFFECTS": 306, "REMOVE_POISONS": 307, "REMOVE_SHACKLES": 308,
    "ERROR": 1000, "MAP": 1001, "AI_ERROR": 1002,
}

# Reverse lookup
PY_ID_TO_NAME = {v: k for k, v in PY_ACT.items()}

# C action ID constants (from lw_action_stream.h)
C_ACT = {
    "USE_WEAPON": 1, "USE_CHIP": 2, "DAMAGE": 3, "HEAL": 4,
    "ADD_EFFECT": 5, "REMOVE_EFFECT": 6, "STACK_EFFECT": 7, "KILL": 8,
    "MOVE": 9, "SLIDE": 10, "TELEPORT": 11, "END_TURN": 12,
    "START_TURN": 13, "INVOCATION": 14, "RESURRECT": 15,
    "VITALITY": 16, "NOVA_VITALITY": 17, "REDUCE_EFFECTS": 18,
    "REMOVE_POISONS": 19, "REMOVE_SHACKLES": 20, "ADD_STATE": 21,
    "CRITICAL": 22, "USE_INVALID": 23,
}
C_ID_TO_NAME = {v: k for k, v in C_ACT.items()}


# ============== Canonical type mapping ======================================
# Each Python action ID maps to a canonical name (or None if uninteresting).
PY_TO_CANON = {
    PY_ACT["USE_WEAPON"]: "USE_WEAPON",
    PY_ACT["USE_CHIP"]:   "USE_CHIP",
    PY_ACT["LOST_LIFE"]:  "DAMAGE",
    PY_ACT["HEAL"]:       "HEAL",
    PY_ACT["ADD_WEAPON_EFFECT"]: "ADD_EFFECT",
    PY_ACT["ADD_CHIP_EFFECT"]:   "ADD_EFFECT",
    PY_ACT["REMOVE_EFFECT"]: "REMOVE_EFFECT",
    PY_ACT["STACK_EFFECT"]:  "STACK_EFFECT",
    PY_ACT["KILL"]:    "KILL",
    PY_ACT["MOVE_TO"]: "MOVE",
    PY_ACT["LEEK_TURN"]: "START_TURN",
    PY_ACT["END_TURN"]:  "END_TURN",
    PY_ACT["SUMMON"]:    "INVOCATION",
    PY_ACT["RESURRECT"]: "RESURRECT",
    PY_ACT["VITALITY"]:    "VITALITY",
    PY_ACT["NOVA_VITALITY"]: "NOVA_VITALITY",
    PY_ACT["REDUCE_EFFECTS"]: "REDUCE_EFFECTS",
    PY_ACT["REMOVE_POISONS"]: "REMOVE_POISONS",
    PY_ACT["REMOVE_SHACKLES"]: "REMOVE_SHACKLES",
    # Ignored (no C-side equivalent in current emit set, or scaffolding)
    PY_ACT["START_FIGHT"]: None,
    PY_ACT["END_FIGHT"]:   None,
    PY_ACT["NEW_TURN"]:    None,
    PY_ACT["PLAYER_DEAD"]: None,
    PY_ACT["SET_WEAPON"]:  None,
    PY_ACT["LOST_PT"]:     None,
    PY_ACT["LOST_PM"]:     None,
    PY_ACT["LOSE_STRENGTH"]: None,
    PY_ACT["DAMAGE_RETURN"]: None,
    PY_ACT["LIFE_DAMAGE"]: None,
    PY_ACT["POISON_DAMAGE"]: None,
    PY_ACT["AFTEREFFECT"]: None,
    PY_ACT["NOVA_DAMAGE"]: None,
    PY_ACT["UPDATE_EFFECT"]: None,
    PY_ACT["MAP"]: None,
    PY_ACT["AI_ERROR"]: None,
    PY_ACT["ERROR"]: None,
    PY_ACT["LAMA"]: None,
    PY_ACT["SAY"]: None,
    PY_ACT["SHOW_CELL"]: None,
    PY_ACT["CHEST_OPENED"]: None,
}
C_TO_CANON = {C_ACT[k]: k for k in C_ACT}


# ============== C-side helpers =============================================

def build_18x18_diamond_topo():
    """Build the same topology as data_c.py:_build_diamond_topology_data."""
    from leekwars_c._engine import Topology
    width = height = 18
    nb_cells = (width * 2 - 1) * height - (width - 1)
    w2m1 = width * 2 - 1
    cells = []
    flags = [None] * nb_cells
    for i in range(nb_cells):
        x_raw = i % w2m1
        y_raw = i // w2m1
        north = west = east = south = True
        if y_raw == 0 and x_raw < width:
            north = False; west = False
        elif y_raw + 1 == height and x_raw >= width:
            east = False; south = False
        if x_raw == 0:
            south = False; west = False
        elif x_raw + 1 == width:
            north = False; east = False
        cy = y_raw - (x_raw % width)
        cx = (i - (width - 1) * cy) // width
        cells.append((i, cx, cy, True))
        flags[i] = (south, west, north, east)
    neighbors = []
    for cid in range(nb_cells):
        south, west, north, east = flags[cid]
        s_ = (cid + width - 1) if south and (cid + width - 1) < nb_cells else -1
        w_ = (cid - width) if west and (cid - width) >= 0 else -1
        n_ = (cid - width + 1) if north and (cid - width + 1) >= 0 else -1
        e_ = (cid + width) if east and (cid + width) < nb_cells else -1
        neighbors.append((s_, w_, n_, e_))
    return Topology.from_grid(width, height, cells, neighbors), width


def setup_c_strict(seed: int, weapon_id: int, raw_spec: dict,
                    a_cell: int, b_cell: int, agility: int = 0):
    """Build a deterministic 1v1 C state with fixed cells."""
    from leekwars_c._engine import (
        State, AttackSpec, catalog_clear, catalog_register,
    )
    catalog_clear()
    spec = AttackSpec(
        item_id=weapon_id, attack_type=1,
        min_range=int(raw_spec["min_range"]),
        max_range=int(raw_spec["max_range"]),
        launch_type=int(raw_spec["launch_type"]),
        area=int(raw_spec["area"]),
        needs_los=1 if raw_spec.get("los", True) else 0,
        tp_cost=int(raw_spec["cost"]),
    )
    for eff in raw_spec.get("effects", []):
        spec.add_effect(
            type=int(eff["type"]),
            value1=float(eff["value1"]),
            value2=float(eff["value2"]),
            turns=int(eff["turns"]),
            targets_filter=int(eff["targets"]),
            modifiers=int(eff["modifiers"]),
        )
    catalog_register(spec)

    topo, _W = build_18x18_diamond_topo()
    s = State()
    s.set_topology(topo)
    s._set_rng(seed)
    # Use FIDs 0 and 1 to match Python's auto-assigned fids
    # (state.mNextEntityId starts at 0).
    # Identical frequencies = same start-order probabilities, so divergence
    # only comes from the underlying RNG (still RNG-driven; we'll address
    # that separately).
    s.add_entity(fid=0, team_id=0, cell_id=a_cell,
                  total_tp=14, total_mp=6, hp=2500, total_hp=2500,
                  weapons=[weapon_id], chips=[],
                  strength=200, agility=agility, frequency=100, level=100)
    s.add_entity(fid=1, team_id=1, cell_id=b_cell,
                  total_tp=14, total_mp=6, hp=2500, total_hp=2500,
                  weapons=[weapon_id], chips=[],
                  strength=200, agility=agility, frequency=100, level=100)
    if os.environ.get("LW_TRACE_RNG"):
        print(f"  [trace c]  before compute_start_order, rng={s.rng_state}")
    s.compute_start_order()
    if os.environ.get("LW_TRACE_RNG"):
        print(f"  [trace c]  after compute_start_order,  rng={s.rng_state}")
    return s


def run_c_strict(s, weapon_id: int, max_turns: int = 30) -> list:
    """Drive the C engine with always-fire scripted policy, capture stream."""
    from leekwars_c._engine import WIN_ONGOING
    s.stream_enable(True)
    s.stream_clear()
    safety = max_turns * 4
    first_shot_traced = False
    for _ in range(safety):
        active = s.next_entity_turn()
        if active < 0:
            break
        s.entity_start_turn(active)
        target = 1 - active
        if not s.entity_alive(target):
            break
        target_cell = s.entity_cell(target)
        for _ in range(8):
            if os.environ.get("LW_TRACE_RNG") and not first_shot_traced:
                print(f"  [trace c]  before first weapon shot, rng={s.rng_state}")
                first_shot_traced = True
            ok = s.apply_action_use_weapon(active, weapon_id=weapon_id,
                                            target_cell_id=target_cell)
            if not ok:
                break
            if not s.entity_alive(target):
                break
        # Emit END_TURN for this entity (mirrors Python's
        # Fight.startTurn -> log(ActionEndTurn(current))).
        s.entity_end_turn(active)
        if s.compute_winner() != WIN_ONGOING:
            break
    return s.stream_dump()


# ============== Python upstream helpers ====================================

def setup_py_strict(seed: int, weapon_id: int, weapons_json: dict,
                     a_cell: int, b_cell: int):
    """Build a deterministic 1v1 Python upstream state.

    Uses a custom_map with no random obstacles, forces specific start
    cells via team1/team2 lists.
    """
    # Generator()._loadWeapons() at runScenario time already loads every
    # weapon in weapons.json under its `item` id. We don't need to
    # pre-register anything — just trust that Weapons.getWeapon(weapon_id)
    # will resolve.

    # Build a custom_map: empty obstacles + fixed team1/team2 cells.
    custom_map = {
        "id": 0,
        "obstacles": {},  # NO random obstacles
        "team1": [a_cell],
        "team2": [b_cell],
    }
    return custom_map


def run_py_strict(seed: int, weapon_id: int, custom_map: dict,
                   weapons_json: dict, max_turns: int = 30,
                   agility: int = 0) -> tuple:
    """Run a Python upstream fight with scripted always-fire agent.
    Returns (action stream JSON list, winner, hps_per_fid).
    """
    from leekwars.generator import Generator
    from leekwars.fight.fight import Fight as _Fight
    from leekwars.statistics.statistics_manager import DefaultStatisticsManager
    from leekwars.scenario.scenario import Scenario
    from leekwars.scenario.farmer_info import FarmerInfo
    from leekwars.scenario.team_info import TeamInfo
    from leekwars.scenario.entity_info import EntityInfo
    from leekwars.state.state import State as PyState
    from leekwars.classes import weapon_class, fight_class

    class _NoOpStats(DefaultStatisticsManager):
        def setGeneratorFight(self, fight): pass
    class _NoOpReg:
        def getRegisters(self, leek): return None
        def saveRegisters(self, leek, registers, is_new): pass

    _debug = {"calls": 0, "fires": 0, "errors": []}
    from leekwars.weapons import weapons as _PyWeapons

    class ScriptedAgent:
        """Always fire equipped weapon at nearest enemy. No movement.

        On first call per entity, equips the weapon DIRECTLY on the entity
        (bypassing state.setWeapon which costs 1 TP + logs SET_WEAPON).
        This mirrors C's `apply_action_use_weapon(entity, weapon_id, ...)`
        semantics where the weapon id is passed per-shot with no equip log.
        Weapon lookup is LAZY (inside __call__) so the Generator() has
        time to populate the registry first.
        """
        def __call__(self, ai):
            _debug["calls"] += 1
            me = ai.getEntity()
            # Force-equip so getWeapon() returns the chosen weapon
            if me.getWeapon() is None:
                w = _PyWeapons.getWeapon(weapon_id)
                if w is None:
                    _debug["errors"].append(f"call {_debug['calls']}: no weapon registered for id {weapon_id}")
                    return
                me.setWeapon(w)
            enemy_id = fight_class.getNearestEnemy(ai)
            if enemy_id < 0:
                _debug["errors"].append(f"call {_debug['calls']}: no enemy")
                return
            enemy = ai.getFight().getEntity(enemy_id)
            for _ in range(8):
                w = me.getWeapon()
                if w is None:
                    _debug["errors"].append(f"call {_debug['calls']}: no weapon (after equip)")
                    break
                if me.getTP() < w.getCost():
                    break
                if not ai.getState().getMap().canUseAttack(
                        me.getCell(), enemy.getCell(), w.getAttack()):
                    _debug["errors"].append(f"call {_debug['calls']}: cant attack from {me.getCell()} to {enemy.getCell()}")
                    break
                if os.environ.get("LW_TRACE_RNG") and _debug["fires"] == 0:
                    print(f"  [trace py] before first weapon shot, rng.n={ai.getState().getRandom().n}")
                try:
                    r = weapon_class.useWeapon(ai, enemy_id)
                    _debug["fires"] += 1
                except Exception as exc:
                    _debug["errors"].append(f"call {_debug['calls']}: useWeapon raised {type(exc).__name__}: {exc}")
                    break
                if r <= 0 or enemy.isDead() or me.isDead():
                    break

    sc = Scenario()
    sc.seed = seed
    sc.maxTurns = max_turns
    sc.type = PyState.TYPE_SOLO
    sc.context = PyState.CONTEXT_TEST
    sc.map = custom_map  # CRITICAL: pass our custom map
    f1 = FarmerInfo(); f1.id = 1; f1.name = "A"; f1.country = "fr"
    f2 = FarmerInfo(); f2.id = 2; f2.name = "B"; f2.country = "fr"
    sc.farmers[1] = f1; sc.farmers[2] = f2
    t1 = TeamInfo(); t1.id = 1; t1.name = "T1"
    t2 = TeamInfo(); t2.id = 2; t2.name = "T2"
    sc.teams[1] = t1; sc.teams[2] = t2

    def mk(eid, fid, name):
        e = EntityInfo()
        e.id = eid; e.name = name; e.type = 0
        e.farmer = fid; e.team = fid
        e.level = 100; e.life = 2500
        e.strength = 200; e.agility = agility
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100  # identical for both
        e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [weapon_id]
        e.chips = []
        e.ai_function = ScriptedAgent()
        return e
    sc.addEntity(0, mk(1, 1, "A"))
    sc.addEntity(1, mk(2, 2, "B"))

    # Hook to capture the Fight instance (Generator doesn't expose it).
    captured = {"fight": None}
    _orig = _Fight.__init__
    def _patched(self, *a, **k):
        _orig(self, *a, **k)
        captured["fight"] = self
    _Fight.__init__ = _patched

    # Force a deterministic start order so RNG divergence on
    # compute_start_order doesn't poison the diff. Both engines will see
    # entity_fid=1 first, then entity_fid=0 (matching C's current output).
    from leekwars.state.start_order import StartOrder as _StartOrder
    _orig_compute = _StartOrder.compute
    def _det_compute(self_, state):
        # Flatten and sort by FId DESC (so [B(fid=1), A(fid=0)]).
        ents = []
        for team in self_.teams:
            ents.extend(team)
        return sorted(ents, key=lambda e: -e.getFId())
    _StartOrder.compute = _det_compute

    # Patch Map.generateMap so the throwaway obstacle_count RNG draw at
    # state.initFight() is REWOUND (one inverse LCG step before the call,
    # so the engine's get_int(30,80) lands on the same n_after that was
    # there before initFight asked for it).
    #
    # ALSO advance the RNG by +2 draws to mirror C's compute_start_order,
    # which makes 2 draws (one per team) that Python's patched _det_compute
    # skips. After both adjustments, Python and C are in sync at the first
    # weapon use.
    from leekwars.maps.map import Map as _PyMap
    _orig_gen = _PyMap.generateMap
    INV_LCG = 0xf7ba6361eeb9eb65
    MASK64 = (1 << 64) - 1
    @staticmethod
    def _wrapped_gen(state, context, w, h, obstacles_count, teams, custom_map):
        rng = state.getRandom()
        if os.environ.get("LW_TRACE_RNG"):
            print(f"  [trace py] generateMap entered, rng.n={rng.n}")
        # Snapshot rng BEFORE all of initFight's RNG consumption (we
        # got here AFTER initFight's obstacle_count draw, so we rewind
        # that one first to land on the pre-init state).
        n_after = rng.n & MASK64
        n_before = ((n_after - 12345) * INV_LCG) & MASK64
        if n_before >= (1 << 63):
            n_before -= (1 << 64)
        snap = n_before
        # Run the real generateMap (consumes RNG draws: per-entity
        # getRandomCell has 2 get_int calls, even when custom team1/team2
        # cells override the result).
        result = _orig_gen(state, context, w, h, obstacles_count, teams, custom_map)
        if os.environ.get("LW_TRACE_RNG"):
            print(f"  [trace py] generateMap after _orig, rng.n={rng.n}")
        # Restore the snapshot (rng is exactly as it was at the start
        # of initFight). Then advance to match C's pre-fight draws,
        # which are exactly compute_start_order = 2 (1 per team).
        rng.n = snap
        n_advance = int(os.environ.get("LW_RNG_ADVANCE", "2"))
        for _ in range(n_advance):
            rng.get_double()
        if os.environ.get("LW_TRACE_RNG"):
            print(f"  [trace py] generateMap after restore+advance({n_advance}), rng.n={rng.n}")
        return result
    _PyMap.generateMap = _wrapped_gen

    try:
        g = Generator()
        out = g.runScenario(sc, None, _NoOpReg(), _NoOpStats())
    finally:
        _Fight.__init__ = _orig
        _StartOrder.compute = _orig_compute
        _PyMap.generateMap = _orig_gen

    fight = captured["fight"]
    state = fight.getState()
    hps = {ent.getFId(): ent.getLife() for ent in state.getEntities().values()}
    # Capture action stream as JSON
    actions = []
    for action in state.actions.actions:
        try:
            actions.append(action.getJSON())
        except Exception:
            pass
    # Stash debug info under attribute for caller
    run_py_strict._last_debug = dict(_debug)
    return actions, out.winner, hps


# ============== Normalization ==============================================

def normalize_py(stream: list) -> list:
    """Normalize Python action stream into (canonical, fid_caster, fid_target,
    primary_value) tuples, dropping uninteresting types."""
    out = []
    for j in stream:
        if not isinstance(j, list) or len(j) == 0:
            continue
        aid = j[0]
        canon = PY_TO_CANON.get(aid)
        if canon is None:
            continue
        # Decode args. Each ActionXxx subclass has its own format; this
        # captures the formats we've actually verified by reading the
        # leekwars/action/*.py source.
        if canon == "DAMAGE":
            # LOST_LIFE = [101, target_fid, pv, erosion]
            target = j[1] if len(j) > 1 else -1
            pv = j[2] if len(j) > 2 else 0
            out.append((canon, -1, target, int(pv)))
        elif canon == "USE_WEAPON":
            # ActionUseWeapon = [16, target_cell, success_code]
            target_cell = j[1] if len(j) > 1 else -1
            success = j[2] if len(j) > 2 else 0
            out.append((canon, -1, target_cell, int(success)))
        elif canon == "USE_CHIP":
            # ActionUseChip = [12, chip_id, target_cell, success_code]
            chip_id = j[1] if len(j) > 1 else -1
            target_cell = j[2] if len(j) > 2 else -1
            out.append((canon, -1, target_cell, int(chip_id)))
        elif canon == "KILL":
            # ActionKill = [11, victim_fid] (no killer in JSON)
            target = j[1] if len(j) > 1 else -1
            out.append((canon, -1, target, 0))
        elif canon == "START_TURN":
            # ActionEntityTurn = [7, leek_fid]
            caster = j[1] if len(j) > 1 else -1
            out.append((canon, caster, -1, 0))
        elif canon == "END_TURN":
            # ActionEndTurn = [8, leek_fid, used_tp, used_mp]
            caster = j[1] if len(j) > 1 else -1
            used_tp = int(j[2]) if len(j) > 2 else 0
            out.append((canon, caster, -1, used_tp))
        elif canon == "MOVE":
            # ActionMove = [10, leek_fid, ...path]
            caster = j[1] if len(j) > 1 else -1
            out.append((canon, caster, -1, 0))
        elif canon in ("HEAL", "VITALITY", "NOVA_VITALITY"):
            caster = j[1] if len(j) > 1 else -1
            target = j[2] if len(j) > 2 else -1
            value = int(j[3]) if len(j) > 3 else 0
            out.append((canon, caster, target, value))
        else:
            arg1 = j[1] if len(j) > 1 else -1
            arg2 = j[2] if len(j) > 2 else -1
            out.append((canon, arg1, arg2, 0))
    return out


def normalize_c(stream: list) -> list:
    """Normalize C action stream. C entries are dicts with keys:
    type, caster, target, v1, v2, v3.

    With C-side fids set to 0 and 1 (matching Python), no offset is needed.
    Per-type adjustments mirror the Python action JSON formats so that
    rows compare equal.
    """
    out = []
    for e in stream:
        canon = C_TO_CANON.get(e["type"])
        if canon == "USE_INVALID" or canon is None:
            continue
        c = e["caster"]; t = e["target"]
        c_fid = -1 if c < 0 else int(c)
        t_fid = -1 if t < 0 else int(t)
        v1 = int(e["v1"])
        if canon == "USE_WEAPON":
            # Python action_use_weapon stores [target_cell, success].
            # C stores caster_idx, target_cell, v1=critical, v2=item_id.
            # Map: caster=-1 (Python doesn't include it),
            #       target=cell, success = 2 (CRITICAL) or 1 (SUCCESS).
            success = 2 if v1 != 0 else 1
            out.append((canon, -1, t_fid, success))
        elif canon == "USE_CHIP":
            # Python action_use_chip = [chip_id, cell, success].
            # C stores caster, cell, v1=critical, v2=item_id.
            success = 2 if v1 != 0 else 1
            chip_id = int(e.get("v2", 0))
            out.append((canon, -1, t_fid, chip_id))
        elif canon == "DAMAGE":
            # Python LOST_LIFE = [target, pv, erosion].
            # C stores caster, target, v1=damage, v2=erosion.
            out.append((canon, -1, t_fid, v1))
        elif canon == "KILL":
            # Python ActionKill = [victim_fid] (no killer).
            # C stores caster=killer, target=victim.
            out.append((canon, -1, t_fid, 0))
        else:
            out.append((canon, c_fid, t_fid, v1))
    return out


# ============== Diff =======================================================

def diff_streams(py_stream: list, c_stream: list, *, loose: bool = False) -> dict:
    """Compare two normalized streams. Returns a dict with:
    - 'identical': bool
    - 'first_divergence_idx': int or -1
    - 'py_only_types': counts of types Python emits that C doesn't
    - 'c_only_types': counts of types C emits that Python doesn't
    - 'matches_per_type': dict canonical -> (n_match, n_total)

    With loose=True, masks the known-divergence sites:
      - drops Python END_TURN (C lacks emit, fix in Phase A.3)
      - drops C KILL on damage-death (Python doesn't emit, fix in C)
      - normalizes DAMAGE pv to 0 (RNG-derived, A.5/A.6 RNG harmonization)
    """
    from collections import Counter
    py_norm = normalize_py(py_stream)
    c_norm = normalize_c(c_stream)
    if loose:
        py_norm = [t for t in py_norm if t[0] != "END_TURN"]
        c_norm = [t for t in c_norm if t[0] != "KILL"]
        py_norm = [(t[0], t[1], t[2], 0) if t[0] == "DAMAGE" else t for t in py_norm]
        c_norm = [(t[0], t[1], t[2], 0) if t[0] == "DAMAGE" else t for t in c_norm]
        # USE_WEAPON success differs (CRITICAL roll). Drop the success code.
        py_norm = [(t[0], t[1], t[2], 0) if t[0] == "USE_WEAPON" else t for t in py_norm]
        c_norm = [(t[0], t[1], t[2], 0) if t[0] == "USE_WEAPON" else t for t in c_norm]
    py_types = Counter(t[0] for t in py_norm)
    c_types = Counter(t[0] for t in c_norm)
    common = set(py_types) & set(c_types)
    py_only = {k: v for k, v in py_types.items() if k not in common}
    c_only = {k: v for k, v in c_types.items() if k not in common}

    # Find first index where the two diverge
    n = min(len(py_norm), len(c_norm))
    first_div = -1
    for i in range(n):
        if py_norm[i] != c_norm[i]:
            first_div = i
            break
    if first_div == -1 and len(py_norm) != len(c_norm):
        first_div = n

    return {
        "identical": first_div == -1,
        "first_divergence_idx": first_div,
        "py_n": len(py_norm), "c_n": len(c_norm),
        "py_types": dict(py_types), "c_types": dict(c_types),
        "py_only_types": py_only, "c_only_types": c_only,
        "py_first_divergent": py_norm[first_div] if 0 <= first_div < len(py_norm) else None,
        "c_first_divergent": c_norm[first_div] if 0 <= first_div < len(c_norm) else None,
    }


# ============== Main entry =================================================

def load_weapons_json() -> dict:
    py_root = None
    for c in (os.environ.get("LEEKWARS_PY_DIR"),
              "C:/Users/aurel/Desktop/leekwars_generator_python",
              "/mnt/c/Users/aurel/Desktop/leekwars_generator_python"):
        if c and os.path.isdir(c):
            py_root = c
            break
    with open(os.path.join(py_root, "data", "weapons.json")) as f:
        return json.load(f)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=10)
    p.add_argument("--weapon", type=int, default=1, help="weapon id (default=1=pistol)")
    p.add_argument("--a-cell", type=int, default=72,
                   help="Cell id for entity A (default 72 = cx=4,cy=0)")
    p.add_argument("--b-cell", type=int, default=144,
                   help="Cell id for entity B (default 144 = cx=8,cy=0)")
    p.add_argument("--agility", type=int, default=0,
                   help="Agility for both entities (0 = no crits)")
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--loose", action="store_true",
                   help="mask known divergences (END_TURN, KILL on damage, "
                        "DAMAGE pv, USE_WEAPON success)")
    args = p.parse_args()

    print("=" * 70)
    print(" Phase A.1 — STRICT action-stream parity (Python upstream vs C)")
    print("=" * 70)

    weapons_json = load_weapons_json()
    # Resolve --weapon as the canonical weapon id (`item` field in
    # weapons.json), not the JSON key. Generator()._loadWeapons registers
    # under `weapon["item"]`, so we must do the same. Build a reverse
    # map from item-id -> raw spec.
    by_item = {int(spec["item"]): spec for spec in weapons_json.values()}
    if args.weapon not in by_item:
        raise SystemExit(f"weapon item id {args.weapon} not in data/weapons.json. "
                         f"Available item ids: {sorted(by_item)[:20]}...")
    raw = by_item[args.weapon]
    print(f" weapon: id={args.weapon} name={raw['name']!r} "
          f"range=[{raw['min_range']},{raw['max_range']}] cost={raw['cost']}")

    A_CELL = args.a_cell
    B_CELL = args.b_cell
    print(f" cells: A={A_CELL} B={B_CELL}  agility={args.agility}")
    print(f" running {args.seeds} seeds...\n")

    n_identical = 0
    aggregated_py_only = {}
    aggregated_c_only = {}
    first_failures = []

    for seed in range(1, args.seeds + 1):
        try:
            # C side
            s = setup_c_strict(seed, args.weapon, raw, A_CELL, B_CELL,
                                agility=args.agility)
            c_stream = run_c_strict(s, args.weapon)

            # Python side
            custom_map = setup_py_strict(seed, args.weapon, weapons_json,
                                          A_CELL, B_CELL)
            py_stream, py_winner, py_hps = run_py_strict(
                seed, args.weapon, custom_map, weapons_json,
                agility=args.agility,
            )
            if args.verbose and seed == 1:
                _dbg = getattr(run_py_strict, "_last_debug", {})
                print(f"  [py debug] agent calls={_dbg.get('calls')} fires={_dbg.get('fires')}")
                for line in (_dbg.get('errors') or [])[:10]:
                    print(f"    {line}")
        except Exception as e:
            print(f"  seed {seed}: ERROR {type(e).__name__}: {e}")
            if args.verbose:
                import traceback as _tb
                _tb.print_exc()
            continue

        diff = diff_streams(py_stream, c_stream, loose=args.loose)
        if diff["identical"]:
            n_identical += 1
            if args.verbose:
                print(f"  seed {seed:>3}: OK (n={diff['py_n']})")
        else:
            if len(first_failures) < 5:
                first_failures.append({"seed": seed, **diff})
            if args.verbose:
                print(f"  seed {seed:>3}: DIVERGE @ idx {diff['first_divergence_idx']}")
                print(f"    py_n={diff['py_n']}  c_n={diff['c_n']}")
                print(f"    py: {diff['py_first_divergent']}")
                print(f"    c:  {diff['c_first_divergent']}")
        for k, v in diff["py_only_types"].items():
            aggregated_py_only[k] = aggregated_py_only.get(k, 0) + v
        for k, v in diff["c_only_types"].items():
            aggregated_c_only[k] = aggregated_c_only.get(k, 0) + v

    print()
    print(f" RESULTS: {n_identical}/{args.seeds} identical")
    print(f" Action types ONLY in Python (C missing): {aggregated_py_only}")
    print(f" Action types ONLY in C    (Python missing): {aggregated_c_only}")

    if first_failures:
        print(f"\n First {len(first_failures)} failures:")
        for f in first_failures:
            print(f"   seed {f['seed']} @ idx {f['first_divergence_idx']}")
            print(f"     py: {f['py_first_divergent']}")
            print(f"     c:  {f['c_first_divergent']}")
            print(f"     py_n={f['py_n']} c_n={f['c_n']}")
            print(f"     py_types: {f['py_types']}")
            print(f"     c_types:  {f['c_types']}")


if __name__ == "__main__":
    main()
