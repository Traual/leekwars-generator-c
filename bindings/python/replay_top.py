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
    _cells_team, _cells_br,
)

BUILDS_DIR = "C:/Users/aurel/Desktop/leeks_builds"

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
                  max_life: int):
    """Build a per-leek AI closure that plays a top-level kit reasonably.

    Args are the leek's loadout + max HP (used for the heal trigger).
    """
    available = set(chips_list)

    # Pre-compute the weapons sorted by their max_range (descending). The
    # AI uses the first weapon whose [min_range, max_range] covers the
    # current cell distance. If none does, it falls back to the weapon
    # with the closest range and tries to reposition.
    weapon_meta = []
    for wid in weapons_list:
        w = base.WEAPONS.get(wid)
        if w is None: continue
        weapon_meta.append((wid, int(w["min_range"]), int(w["max_range"])))
    if not weapon_meta:
        # Should never happen on a real build, but guard anyway.
        weapon_meta.append((37, 1, 7))

    longest_max = max(mr for (_, _, mr) in weapon_meta)

    def _pick_weapon(distance: int) -> int:
        for wid, mn, mx in weapon_meta:
            if mn <= distance <= mx:
                return wid
        # No exact fit -- return the weapon with the closest reach.
        return min(weapon_meta, key=lambda t: abs(distance - t[2]))[0]

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

        # 3. Reposition if out of weapon range. Use teleportation for
        # big jumps, jump for medium, walk otherwise.
        target_cell = eng.entity_cell(target)
        cur_d = eng.cell_distance(eng.entity_cell(idx), target_cell)
        if cur_d > longest_max:
            if 59 in available and cur_d > 8:
                # Teleportation: aim for a cell within max_range of the
                # target. The engine just needs *a* legal cell -- use the
                # target's cell and let the engine snap if invalid.
                eng.use_chip(idx, 59, target_cell)
            elif 144 in available:
                eng.use_chip(idx, 144, target_cell)
            # Walk to close any remaining gap.
            target_cell = eng.entity_cell(target)
            cur_d = eng.cell_distance(eng.entity_cell(idx), target_cell)
            if cur_d > longest_max:
                budget = min(10, max(0, cur_d - longest_max))
                if budget > 0:
                    eng.move_toward(idx, target_cell, max_mp=budget)

        # 4. Offensive chips: AoE first, then poison, then specials.
        target_cell = eng.entity_cell(target)
        for cid in _OFFENSIVE_AOE + _OFFENSIVE_POISON + _OFFENSIVE_OTHER:
            if cid in available and eng.entity_alive(target):
                target_cell = eng.entity_cell(target)
                eng.use_chip(idx, cid, target_cell)

        # 5. Fire the right weapon for the current range, repeat until
        # rejected (out of TP / target dead / out of range).
        for _ in range(12):
            if not eng.entity_alive(target): break
            target_cell = eng.entity_cell(target)
            cur_d = eng.cell_distance(eng.entity_cell(idx), target_cell)
            wid = _pick_weapon(cur_d)
            rc = eng.fire_weapon(idx, wid, target_cell)
            if rc <= 0: break
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
                  seed: int = 1, max_turns: int = 30):
    """``per_team`` vs ``per_team`` with a build per leek (cycled if the
    name list is shorter than the team count)."""
    builds = [load_build(n) for n in build_names]
    eng = _setup_engine_for_builds(builds)
    cells_a = _cells_team(0, per_team)
    cells_b = _cells_team(1, per_team)

    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    leeks = []
    ai_per_entity: dict[int, callable] = {}

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
    eng.set_custom_map(obstacles={}, team1=cells_a, team2=cells_b, map_id=1)

    # Dispatch AI by entity index. The engine calls the callback with
    # (idx, turn). Each leek runs its own build's AI logic.
    def dispatch(idx, turn):
        f = ai_per_entity.get(idx)
        return f(idx, turn) if f else 0
    eng.set_ai_callback(dispatch)
    eng.run()

    team1_ids = [leek["id"] for leek in leeks if leek["team"] == 1]
    team2_ids = [leek["id"] for leek in leeks if leek["team"] == 2]
    return eng, leeks, team1_ids, team2_ids


def run_top_br(n_players: int, build_names: list[str], *,
                seed: int = 1, max_turns: int = 30):
    """``n_players``-way battle royale, one build per slot (cycled)."""
    builds = [load_build(n) for n in build_names]
    eng = _setup_engine_for_builds(builds)
    cells = _cells_br(n_players)

    leeks = []
    ai_per_entity: dict[int, callable] = {}

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
        )

    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_type(TYPE_BATTLE_ROYALE)
    eng.set_custom_map(obstacles={}, team1=cells, team2=[], map_id=1)

    def dispatch(idx, turn):
        f = ai_per_entity.get(idx)
        return f(idx, turn) if f else 0
    eng.set_ai_callback(dispatch)
    eng.run()

    return eng, leeks, [l["id"] for l in leeks], []


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
    "top_1v1":  dict(mode="team", per_team=1,  builds=_TOP_ROTATION[:2]),
    "top_4v4":  dict(mode="team", per_team=4,  builds=_TOP_ROTATION[:5]),
    "top_br10": dict(mode="br",   n_players=10, builds=_TOP_ROTATION),
}


def run_top_scenario(name: str, *, seed: int = 1, max_turns: int = 30):
    cfg = SCENARIOS_TOP[name]
    if cfg["mode"] == "team":
        return run_top_team(cfg["per_team"], cfg["builds"],
                             seed=seed, max_turns=max_turns)
    elif cfg["mode"] == "br":
        return run_top_br(cfg["n_players"], cfg["builds"],
                           seed=seed, max_turns=max_turns)
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
            print(f"  {name:<14}  [{kind:<6}]  builds={cfg['builds']}")
        return

    if args.scenario is None:
        p.error("--scenario is required (or use --list)")
    if args.scenario not in SCENARIOS_TOP:
        p.error(f"unknown scenario {args.scenario!r}")

    eng, leeks, t1, t2 = run_top_scenario(args.scenario,
                                            seed=args.seed,
                                            max_turns=args.turns)
    report = build_report(eng, leeks, team1_ids=t1, team2_ids=t2)
    write_report(args.out, report)
    n = len(report["fight"]["actions"])
    w = int(report.get("winner", -1))
    print(f"wrote {args.out}")
    print(f"  scenario: {args.scenario}")
    print(f"  actions:  {n}")
    print(f"  winner:   {w}")


if __name__ == "__main__":
    main()
