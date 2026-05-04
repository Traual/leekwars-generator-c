"""Top-level replay scenarios using real leek builds + smart AI.

Loads JSON builds from ``Desktop/leeks_builds/`` (level 301 stuff with full
stats / weapons / chips) and runs them against each other so we can watch
high-level fights in the leek-wars-client viewer.

The AI here is more sophisticated than the one in ``replay_stress.py``:
each turn, in order, it tries to

    1. Cast permanent buffs on self (knowledge / adrenaline / steroid /
       protein / wizardry / manumission), turn 1 only.
    2. Cast a defensive turn-1 chip (bramble) on self.
    3. Cast healing chips on self if HP < 60% (regeneration / remission /
       serum / antidote).
    4. Long-jump (jump / teleportation) toward the nearest enemy if it's
       outside the leek's longest weapon range.
    5. Cast the leek's offensive AoE chips on the target's cell (meteorite /
       stalactite / iceberg / rockfall / lightning / spark / flash / etc.).
    6. Cast poison chips (toxin / plague / arsenic).
    7. Fire the build's primary weapon (the first one in selectedWeaponIds
       that fits the current range), repeated until TP exhausts.

This isn't a near-optimal fighter -- a real bot would cycle weapons,
manage cooldowns smarter, reason about opportunity costs. The goal here
is simply to drive enough realistic action so the engine + viewer get
exercised on a *full kit*, not just pistol+flame.

Usage::

    # See what scenarios are available
    python replay_top.py --list

    # Run one
    python replay_top.py --scenario top_1v1
    python replay_top.py --scenario top_4v4
    python replay_top.py --scenario top_br10

The output report.json lands in the leek-wars-client public/static/
folder, ready to view at ``http://localhost:8080/fight/local``.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Iterable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

import leekwars_c._engine as _v2
from leekwars_c.replay import build_leek, build_report, write_report
import test_v2_summon as base   # WEAPONS / CHIPS / register_all_chips_v2

from replay_stress import (
    DEFAULT_REPORT,
    TYPE_TEAM, TYPE_BATTLE_ROYALE,
    _cells_team, _cells_br, _cell_at_pixel, _MAP_MOD, _MAP_W,
)


def _generate_obstacles(reserved: set[int], count: int = 8,
                         seed: int = 1) -> dict[int, int]:
    """Generate ``count`` obstacles using the same shape vocabulary as
    upstream Java (small or 2x2 large) and only template ids that the
    engine actually recognizes via ObstacleInfo.

    Returns ``{cell_id: template_id}`` suitable for
    ``set_custom_map(obstacles=...)``. The engine's processing of this
    dict already handles claiming the E / S / SE neighbors for size-2
    obstacles -- we just need to make sure those neighbors aren't
    spawn cells or already used.
    """
    import random
    rng = random.Random(seed)
    nb_cells = _MAP_MOD * _MAP_W - (_MAP_W - 1)   # 613

    # Candidate cells: interior of the diamond, not a spawn.
    candidates: list[int] = []
    for cell_id in range(nb_cells):
        if cell_id in reserved:
            continue
        px = (cell_id % _MAP_MOD) * 2
        py = (cell_id // _MAP_MOD) * 2
        if px > _MAP_MOD:
            px = px % _MAP_MOD
            py += 1
        if 6 <= px <= 28 and 6 <= py <= 28:
            candidates.append(cell_id)

    rng.shuffle(candidates)
    out: dict[int, int] = {}
    used = set(reserved)

    for cell_id in candidates:
        if len(out) >= count: break
        if cell_id in used: continue

        # Try a 2x2 first (looks better in the viewer); fall back to
        # 1x1 if any of the E/S/SE neighbors are reserved or off-map.
        size_2 = rng.random() < 0.4   # ~40% large
        if size_2:
            e  = cell_id + _MAP_W           # +18  (Pathfinding.EAST)
            s  = cell_id + _MAP_W - 1       # +17  (Pathfinding.SOUTH)
            se = s + _MAP_W                 # +35
            if all(0 <= c < nb_cells and c not in used for c in (e, s, se)):
                out[cell_id] = rng.choice(_LARGE_OBSTACLES)
                used.update({cell_id, e, s, se})
                continue

        # Size-1 fallback.
        out[cell_id] = rng.choice(_SMALL_OBSTACLES)
        used.add(cell_id)
    return out


def _build_weapon_template_map() -> None:
    """Populate ``_WEAPON_ITEM_TO_TEMPLATE`` from the upstream weapons
    JSON. Runs once on first scenario invocation."""
    if _WEAPON_ITEM_TO_TEMPLATE:
        return
    for w in base.WEAPONS.values():
        try:
            item_id = int(w["item"])
            tmpl    = int(w["template"])
            _WEAPON_ITEM_TO_TEMPLATE[item_id] = tmpl
        except (KeyError, TypeError, ValueError):
            pass


def _obstacles_to_client_format(engine_obstacles: dict[int, int],
                                  map_w: int = 18) -> dict:
    """Convert ``{cell_id: template_id}`` (engine format) into the format
    leek-wars-client's game.ts expects.

    The client reads ``data.map.obstacles[cell_id]`` and parses it as
    either:
      * ``[type, size]`` array  -- main cell of an obstacle
      * scalar ``-1``           -- "this cell is part of someone else's
                                   obstacle, skip"
    where ``size`` is the geometry index (1 = small, 2 = 2x2 large).

    For each size-2 obstacle the upstream code marks the E / S / SE
    neighbors as -1 (so the client doesn't try to place another
    obstacle on top of them). We do the same here.

    The engine's ObstacleInfo registry maps template ids to sizes; we
    encode that lookup via ``_LARGE_OBSTACLES`` (size 2) and
    ``_SMALL_OBSTACLES`` (size 1).
    """
    if not engine_obstacles:
        return {}

    large = set(_LARGE_OBSTACLES)
    out: dict[str, list | int] = {}
    for cell_id, template in engine_obstacles.items():
        if template in large:
            out[str(cell_id)] = [template, 2]
            # E / S / SE neighbors stored as -1 markers.
            for nb in (cell_id + map_w,         # EAST
                       cell_id + map_w - 1,     # SOUTH
                       cell_id + 2 * map_w - 1):# SE
                out[str(nb)] = -1
        else:
            out[str(cell_id)] = [template, 1]
    return out


def _inject_set_weapon(events: list[dict],
                        fires_per_entity: dict[int, list[int]]) -> list[dict]:
    """Walk the engine's action stream and insert a synthetic SET_WEAPON
    action ahead of every USE_WEAPON whose weapon differs from what the
    leek currently has equipped.

    The engine's USE_WEAPON action has no weapon-id field (Java upstream
    format is just ``[16, target_cell, success]``), so the
    leek-wars-client renders every USE_WEAPON with whatever weapon was
    auto-equipped first. To keep the visual in sync with what the AI
    actually fired, we replay the AI's own decisions: ``fires_per_entity``
    is the per-leek list of weapons the AI tried to fire IN ORDER, and
    we match it to the USE_WEAPON actions in the stream. When the
    weapon changes, we prepend a SET_WEAPON [13, template_id].
    """
    _build_weapon_template_map()
    out: list[dict] = []
    current_player: int | None = None
    fire_idx: dict[int, int] = {}
    equipped: dict[int, int] = {}   # entity_idx -> weapon template id

    for ev in events:
        t = ev["type"]
        if t == _T_LEEK_TURN:
            current_player = ev["args"][0]
            out.append(ev)
            continue
        if t == _T_USE_WEAPON and current_player is not None:
            log = fires_per_entity.get(current_player, [])
            i = fire_idx.get(current_player, 0)
            if i < len(log):
                wid = log[i]
                fire_idx[current_player] = i + 1
                template = _WEAPON_ITEM_TO_TEMPLATE.get(wid)
                if template is not None and equipped.get(current_player) != template:
                    out.append({
                        "type": _T_SET_WEAPON,
                        "args": [template],
                        "extra": [],
                    })
                    equipped[current_player] = template
        out.append(ev)
    return out

BUILDS_DIR = "C:/Users/aurel/Desktop/leeks_builds"

# Real obstacle template IDs from upstream Java's ObstacleInfo registry
# (java_reference/src/.../maps/ObstacleInfo.java). Anything outside this
# registry gets ignored by the engine -- which is why our earlier
# "1..8" placeholders never blocked movement or LoS.
_SMALL_OBSTACLES = (
    5, 20, 21, 22, 31, 32, 38, 40, 41, 42, 48,
    50, 53, 55, 57, 59, 62, 63, 66,
)   # size 1 (one cell)
_LARGE_OBSTACLES = (
    11, 17, 18, 34, 43, 44, 45, 46, 47, 49,
    52, 54, 56, 58, 61, 64, 65,
)   # size 2 (2x2 cells: main + E + S + SE)


# Action type constants we care about during the SET_WEAPON post-process.
_T_LEEK_TURN = 7
_T_USE_WEAPON = 16
_T_SET_WEAPON = 13


# Map item_id -> weapon template id (the value SET_WEAPON expects).
# Pre-computed once from base.WEAPONS.
_WEAPON_ITEM_TO_TEMPLATE: dict[int, int] = {}

# ---- Build loading ------------------------------------------------------
# Mirrors total_stats_from_build in cma_train/scenario.py (which mirrors
# Java ScenarioGenerator.GetTotalStatsFromEntityBuild). Keep in sync.

_BASE_LEEK_FIXED = {
    "strength": 0, "wisdom": 0, "agility": 0, "resistance": 0,
    "science": 0, "magic": 0, "frequency": 100, "cores": 1,
    "ram": 6, "tp": 10, "mp": 3,
}
_STAT_KEYS = (
    "life", "strength", "wisdom", "agility", "resistance", "science",
    "magic", "frequency", "cores", "ram", "tp", "mp",
)


def total_stats_from_build(build: dict) -> dict:
    """Compute the engine stats from a build's invested + bonus + base.

    Base life is 100 + (level - 1) * 3. Other base values come from the
    fixed leek defaults dict above.
    """
    level = int(build["level"])
    invested = build.get("investedStats", {})
    bonus = build.get("bonusStats", {})

    out = {k: 0 for k in _STAT_KEYS}
    out["life"] = 100 + (level - 1) * 3
    for k, v in _BASE_LEEK_FIXED.items():
        out[k] = v
    for k in _STAT_KEYS:
        out[k] += int(invested.get(k, 0)) + int(bonus.get(k, 0))
    return out


def load_build(name: str) -> dict:
    """Load ``<BUILDS_DIR>/<name>.json`` and stash the leek name on it."""
    path = name if os.path.isabs(name) else os.path.join(BUILDS_DIR, f"{name}.json")
    with open(path) as f:
        b = json.load(f)
    # The build JSONs don't carry the leek's name themselves; we keep the
    # filename as our display name so the viewer's HP bars are readable.
    b.setdefault("_label", os.path.splitext(os.path.basename(path))[0])
    return b


# ---- Smart AI -----------------------------------------------------------
# Chip categorization. Sources: builds across the top 25 + chip effects
# table from chips.json.
#
# Priority order: turn-1 buffs first, then heals if hurt, then movement,
# then offensive chips on the target, finally weapon attacks.

_BUFFS_TURN1 = (
    155,    # knowledge   - +wisdom (heal scaling)
    16,     # adrenaline  - +agility (crit/dodge scaling)
    25,     # steroid     - +strength (cd 5)
    8,      # protein     - +strength (cd 3)
    156,    # wizardry    - +science (chip damage)
    174,    # manumission - +magic
    154,    # elevation   - +MP / utility
    67,     # armoring    - +MP
    172,    # bramble     - damage return shield
)
_HEALS = (
    35,     # regeneration
    80,     # remission
    168,    # serum
    110,    # antidote (also cures poison)
    34,     # liberation (gives back TP -- not quite a heal but close)
)
_OFFENSIVE_AOE = (
    36, 30, 31, 32,         # meteorite, stalactite, iceberg, rockfall
    33, 18, 6, 105, 143,    # lightning, spark, flash, burning, plasma
    7, 5, 19, 2, 1,         # rock, flame, pebble, ice, shock
)
_OFFENSIVE_POISON = (
    98, 99, 171,            # toxin, plague, arsenic
)
_OFFENSIVE_OTHER = (
    114, 95, 68,            # punishment, soporific, inversion
)
_MOVEMENT = (
    59,     # teleportation (long range, expensive)
    144,    # jump (short, cheap)
)




def _make_top_ai(eng, weapons_list: list[int], chips_list: list[int],
                  max_life: int, fires_log: dict[int, list[int]] | None = None):
    """Build a per-leek AI closure that plays a top-level kit reasonably.

    Args are the leek's loadout + max HP (used for the heal trigger).
    ``fires_log`` (if given) is the shared dict ``{entity_idx: [item_ids]}``
    that records every successful weapon shot in order, so the report
    serializer can inject SET_WEAPON when the AI swaps weapons.
    """
    available = set(chips_list)

    # Pre-compute (item_id, min_range, max_range, cost) for every
    # weapon in the kit. The AI iterates this list per turn, picking
    # the cheapest weapon whose [min,max] band covers the current
    # cell distance. Each successful fire is logged so the report
    # serializer can inject SET_WEAPON between consecutive shots
    # whose weapon changes.
    weapon_meta: list[tuple[int, int, int, int]] = []
    for wid in weapons_list:
        w = base.WEAPONS.get(wid)
        if w is None: continue
        weapon_meta.append((wid, int(w["min_range"]),
                              int(w["max_range"]), int(w["cost"])))
    if not weapon_meta:
        weapon_meta.append((37, 1, 7, 3))   # pistol fallback

    # Distance to close to: smallest min_range across the kit -- that
    # way at least the shortest-range weapon can fire. Melee builds
    # close to 1; long-range-only builds stop further.
    preferred_dist = max(1, min(mn for (_, mn, _, _) in weapon_meta))

    def ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        my_hp = eng.entity_hp(idx)
        hp_ratio = my_hp / max_life if max_life > 0 else 1.0

        # Pick nearest enemy.
        target = None; bd = 10**9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue
            d = eng.cell_distance2(my_cell, eng.entity_cell(i))
            if d < bd: bd = d; target = i
        if target is None: return 0
        target_cell = eng.entity_cell(target)

        # 1. Turn-1 buffs.
        if turn == 1:
            for cid in _BUFFS_TURN1:
                if cid in available:
                    eng.use_chip(idx, cid, my_cell)

        # 2. Heal if hurt.
        if hp_ratio < 0.6:
            for cid in _HEALS:
                if cid in available:
                    eng.use_chip(idx, cid, my_cell)

        # 3. Close on the enemy. First try teleportation/jump if the
        # gap is too big to walk, then walk the rest. We aim for the
        # build's preferred distance (1 if melee in kit, else closest
        # min_range) -- *not* longest_max, because line-constrained
        # weapons (launch_type=1) need alignment, which we don't get
        # by camping at max_range.
        target_cell = eng.entity_cell(target)
        cur_d = eng.cell_distance(eng.entity_cell(idx), target_cell)
        if cur_d > preferred_dist:
            if 59 in available and cur_d > 8:
                eng.use_chip(idx, 59, target_cell)
            elif 144 in available and cur_d > 3:
                eng.use_chip(idx, 144, target_cell)
            # Walk the remaining gap with full MP -- movement uses MP,
            # firing uses TP, so spending all MP doesn't compete with
            # the weapon attacks below.
            target_cell = eng.entity_cell(target)
            cur_d = eng.cell_distance(eng.entity_cell(idx), target_cell)
            if cur_d > preferred_dist:
                eng.move_toward(idx, target_cell, max_mp=20)

        # 4. Offensive chips: AoE first, then poison, then specials.
        target_cell = eng.entity_cell(target)
        for cid in _OFFENSIVE_AOE + _OFFENSIVE_POISON + _OFFENSIVE_OTHER:
            if cid in available and eng.entity_alive(target):
                target_cell = eng.entity_cell(target)
                eng.use_chip(idx, cid, target_cell)

        # 5. Pick the cheapest in-range weapon for the current
        # distance and fire it until rejected. If it fails on the
        # first shot, try the next weapon up. Every successful fire
        # is recorded in fires_log so we can inject SET_WEAPON during
        # post-processing (the client visual otherwise stays on the
        # first auto-equipped weapon forever).
        target_cell = eng.entity_cell(target)
        for _ in range(12):
            if not eng.entity_alive(target): break
            target_cell = eng.entity_cell(target)
            cur_d = eng.cell_distance(eng.entity_cell(idx), target_cell)
            # Weapons whose range band covers our current distance,
            # cheapest first (so we get the most shots out of TP).
            in_range = [t for t in weapon_meta
                         if t[1] <= cur_d <= t[2]]
            in_range.sort(key=lambda t: t[3])
            fired = False
            for wid, mn, mx, _cost in in_range:
                rc = eng.fire_weapon(idx, wid, target_cell)
                if rc > 0:
                    if fires_log is not None:
                        fires_log.setdefault(idx, []).append(wid)
                    fired = True
                    break
            if not fired: break
        return 0

    return ai


# ---- Engine setup -------------------------------------------------------

def _setup_engine_for_build(build: dict) -> _v2.Engine:
    """Add every weapon in the build's selectedWeaponIds to the engine
    and register every chip the engine knows about."""
    eng = _v2.Engine()
    for wid in build["selectedWeaponIds"]:
        raw = base.WEAPONS.get(wid)
        if raw is None: continue
        eng.add_weapon(
            item_id=wid, name=raw["name"], cost=int(raw["cost"]),
            min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
            launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
            los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
            effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                      int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                     for e in raw["effects"]],
            passive_effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw.get("passive_effects", [])],
        )
    base.register_all_chips_v2(eng)
    return eng


def _setup_engine_for_builds(builds: list[dict]) -> _v2.Engine:
    """Same as above but for many builds: union the weapon set."""
    eng = _v2.Engine()
    seen: set[int] = set()
    for build in builds:
        for wid in build["selectedWeaponIds"]:
            if wid in seen: continue
            raw = base.WEAPONS.get(wid)
            if raw is None: continue
            eng.add_weapon(
                item_id=wid, name=raw["name"], cost=int(raw["cost"]),
                min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                          int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                         for e in raw["effects"]],
                passive_effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                                  int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                                 for e in raw.get("passive_effects", [])],
            )
            seen.add(wid)
    base.register_all_chips_v2(eng)
    return eng


# ---- Scenario runners ---------------------------------------------------

def run_top_team(per_team: int, build_names: list[str], *,
                  seed: int = 1, max_turns: int = 30,
                  obstacles: int = 0):
    """``per_team`` vs ``per_team`` with a build per leek (cycled if the
    name list is shorter than the team count). ``obstacles`` is the
    number of random small obstacles to drop on the map (avoiding
    spawn cells)."""
    builds = [load_build(n) for n in build_names]
    eng = _setup_engine_for_builds(builds)
    cells_a = _cells_team(0, per_team)
    cells_b = _cells_team(1, per_team)

    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    leeks = []
    ai_per_entity: dict[int, callable] = {}
    fires_log: dict[int, list[int]] = {}

    def _add(team_engine: int, team_id: int, fid: int, name: str, cell: int,
              build: dict):
        stats = total_stats_from_build(build)
        eng.add_farmer(fid, name, "fr")
        eng.add_entity(
            team=team_engine, fid=fid, name=name, level=int(build["level"]),
            life=stats["life"], tp=stats["tp"], mp=stats["mp"],
            strength=stats["strength"], agility=stats["agility"],
            frequency=stats["frequency"], wisdom=stats["wisdom"],
            resistance=stats["resistance"], science=stats["science"],
            magic=stats["magic"], cores=stats["cores"], ram=stats["ram"],
            farmer=fid, team_id=team_id,
            weapons=list(build["selectedWeaponIds"]),
            chips=list(build["selectedChipIds"]),
            cell=cell,
        )
        leeks.append(build_leek(
            id=fid - 1, team=team_id, name=name, cell=cell,
            level=int(build["level"]),
            life=stats["life"], strength=stats["strength"],
            agility=stats["agility"], wisdom=stats["wisdom"],
            resistance=stats["resistance"], science=stats["science"],
            magic=stats["magic"], frequency=stats["frequency"],
            tp=stats["tp"], mp=stats["mp"],
            weapons=list(build["selectedWeaponIds"]),
            chips=list(build["selectedChipIds"]),
            farmer=fid,
        ))
        ai_per_entity[fid - 1] = _make_top_ai(
            eng,
            list(build["selectedWeaponIds"]),
            list(build["selectedChipIds"]),
            stats["life"],
            fires_log=fires_log,
        )

    fid = 1
    for i in range(per_team):
        b = builds[i % len(builds)]
        _add(0, 1, fid, f"A{i+1}_{b['_label'][:10]}", cells_a[i], b)
        fid += 1
    for i in range(per_team):
        b = builds[(i + 1) % len(builds)]   # mirror but offset so opposing
                                            # leeks aren't always identical
        _add(1, 2, fid, f"B{i+1}_{b['_label'][:10]}", cells_b[i], b)
        fid += 1

    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_type(TYPE_TEAM if per_team > 1 else 1)
    obs_dict = _generate_obstacles(set(cells_a) | set(cells_b),
                                     count=obstacles, seed=seed) if obstacles else {}
    eng.set_custom_map(obstacles=obs_dict, team1=cells_a, team2=cells_b, map_id=1)

    # Dispatch AI by entity index. The engine calls the callback with
    # (idx, turn). Each leek runs its own build's AI logic.
    def dispatch(idx, turn):
        f = ai_per_entity.get(idx)
        return f(idx, turn) if f else 0
    eng.set_ai_callback(dispatch)
    eng.run()

    team1_ids = [leek["id"] for leek in leeks if leek["team"] == 1]
    team2_ids = [leek["id"] for leek in leeks if leek["team"] == 2]
    return eng, leeks, team1_ids, team2_ids, obs_dict, fires_log


def run_top_br(n_players: int, build_names: list[str], *,
                seed: int = 1, max_turns: int = 30,
                obstacles: int = 0):
    """``n_players``-way battle royale, one build per slot (cycled).
    ``obstacles`` adds random walls in the central rectangle."""
    builds = [load_build(n) for n in build_names]
    eng = _setup_engine_for_builds(builds)
    cells = _cells_br(n_players)

    leeks = []
    ai_per_entity: dict[int, callable] = {}
    fires_log: dict[int, list[int]] = {}

    for i in range(n_players):
        eng.add_team(i + 1, f"T{i+1}")
        eng.add_farmer(i + 1, f"P{i+1}", "fr")
    for i in range(n_players):
        b = builds[i % len(builds)]
        stats = total_stats_from_build(b)
        name = f"P{i+1}_{b['_label'][:10]}"
        eng.add_entity(
            team=i, fid=i+1, name=name, level=int(b["level"]),
            life=stats["life"], tp=stats["tp"], mp=stats["mp"],
            strength=stats["strength"], agility=stats["agility"],
            frequency=stats["frequency"], wisdom=stats["wisdom"],
            resistance=stats["resistance"], science=stats["science"],
            magic=stats["magic"], cores=stats["cores"], ram=stats["ram"],
            farmer=i+1, team_id=i+1,
            weapons=list(b["selectedWeaponIds"]),
            chips=list(b["selectedChipIds"]),
            cell=cells[i],
        )
        leeks.append(build_leek(
            id=i, team=i+1, name=name, cell=cells[i],
            level=int(b["level"]),
            life=stats["life"], strength=stats["strength"],
            agility=stats["agility"], wisdom=stats["wisdom"],
            resistance=stats["resistance"], science=stats["science"],
            magic=stats["magic"], frequency=stats["frequency"],
            tp=stats["tp"], mp=stats["mp"],
            weapons=list(b["selectedWeaponIds"]),
            chips=list(b["selectedChipIds"]),
            farmer=i+1,
        ))
        ai_per_entity[i] = _make_top_ai(
            eng,
            list(b["selectedWeaponIds"]),
            list(b["selectedChipIds"]),
            stats["life"],
            fires_log=fires_log,
        )

    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_type(TYPE_BATTLE_ROYALE)
    obs_dict = _generate_obstacles(set(cells), count=obstacles,
                                     seed=seed) if obstacles else {}
    eng.set_custom_map(obstacles=obs_dict, team1=cells, team2=[], map_id=1)

    def dispatch(idx, turn):
        f = ai_per_entity.get(idx)
        return f(idx, turn) if f else 0
    eng.set_ai_callback(dispatch)
    eng.run()

    return eng, leeks, [l["id"] for l in leeks], [], obs_dict, fires_log


# ---- Scenario library ---------------------------------------------------

# Curated rotation of named builds: cover damage profiles (magic, strength,
# science, mixed), weapon archetypes (melee, mid-range, long-range), and
# build philosophies (tank, glass-cannon, summoner).
_TOP_ROTATION = [
    "Claudios",        # magic + strength hybrid, flame_thrower / heavy_sword
    "Kraneur",         # magic, sword + flame_thrower
    "LeekyMinaj",      # strength, illicit_grenade_launcher / bazooka / heavy_sword
    "hardleeker",      # science, rhino + electrisor + heavy_sword
    "SuperPanda",      # strength, electrisor + lightninger + rifle + flame
    "BlueSlash",
    "ArkansasBlack",
    "Lamastico",
    "Scotty1",
    "Astheopyrosphamidum",
]

SCENARIOS_TOP: dict[str, dict] = {
    "top_1v1":         dict(mode="team", per_team=1,  builds=_TOP_ROTATION[:2]),
    "top_4v4":         dict(mode="team", per_team=4,  builds=_TOP_ROTATION[:5]),
    "top_br10":        dict(mode="br",   n_players=10, builds=_TOP_ROTATION),
    # With obstacles -- the central rectangle gets ~8 small walls that
    # block line of sight, forcing leeks to reposition around them.
    "top_1v1_obst":    dict(mode="team", per_team=1,  builds=_TOP_ROTATION[:2],
                              obstacles=6),
    "top_4v4_obst":    dict(mode="team", per_team=4,  builds=_TOP_ROTATION[:5],
                              obstacles=8),
    "top_br10_obst":   dict(mode="br",   n_players=10, builds=_TOP_ROTATION,
                              obstacles=10),
}


def run_top_scenario(name: str, *, seed: int = 1, max_turns: int = 30):
    cfg = SCENARIOS_TOP[name]
    obstacles = cfg.get("obstacles", 0)
    if cfg["mode"] == "team":
        return run_top_team(cfg["per_team"], cfg["builds"],
                             seed=seed, max_turns=max_turns,
                             obstacles=obstacles)
    elif cfg["mode"] == "br":
        return run_top_br(cfg["n_players"], cfg["builds"],
                           seed=seed, max_turns=max_turns,
                           obstacles=obstacles)
    raise ValueError(cfg["mode"])


# ---- CLI ----------------------------------------------------------------

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--list", action="store_true")
    p.add_argument("--scenario", default=None,
                    help="Name of a top scenario (top_1v1 / top_4v4 / top_br10).")
    p.add_argument("--out", default=DEFAULT_REPORT)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--turns", type=int, default=30)
    args = p.parse_args()

    if args.list:
        print("Available top scenarios:")
        for name, cfg in SCENARIOS_TOP.items():
            kind = (f"{cfg['per_team']}v{cfg['per_team']}" if cfg["mode"] == "team"
                    else f"BR {cfg['n_players']}")
            obs = f" obstacles={cfg['obstacles']}" if cfg.get("obstacles") else ""
            print(f"  {name:<18}  [{kind:<6}]{obs}  builds={cfg['builds']}")
        return

    if args.scenario is None:
        p.error("--scenario is required (or use --list)")
    if args.scenario not in SCENARIOS_TOP:
        p.error(f"unknown scenario {args.scenario!r}")

    eng, leeks, t1, t2, obs, fires_log = run_top_scenario(
        args.scenario, seed=args.seed, max_turns=args.turns)
    # Inject SET_WEAPON before each USE_WEAPON whose weapon differs
    # from the leek's current equip. Without this, the client visual
    # locks to the first weapon ever fired and stays there forever.
    events = _inject_set_weapon(list(eng.stream_dump()), fires_log)
    # Convert engine obstacles (scalar template ids) to the client's
    # expected format (arrays + neighbor -1 markers).
    client_obs = _obstacles_to_client_format(obs)
    report = build_report(eng, leeks, team1_ids=t1, team2_ids=t2,
                            obstacles=client_obs, events_override=events)
    write_report(args.out, report)
    n = len(report["fight"]["actions"])
    w = int(report.get("winner", -1))
    print(f"wrote {args.out}")
    print(f"  scenario: {args.scenario}")
    print(f"  actions:  {n}")
    print(f"  winner:   {w}")


if __name__ == "__main__":
    main()
