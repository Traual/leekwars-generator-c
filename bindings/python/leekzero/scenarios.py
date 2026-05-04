"""Scenario builders for the Leek-Zero curriculum.

Each function takes a seed and returns a fully-configured Engine
ready to ``run()``. The seed drives:
  * Spawn cells (drawn from a per-side pool so leeks land on
    opposite halves of the map).
  * Obstacle layout (count + cells + sizes + types).
  * Engine RNG seed for damage rolls / criticals / etc.

Curriculum stages exposed here:

  stage 1  make_1v1_stage1_static
           1v1 pistol+flame, fixed cells (72 vs 144), no obstacles.
           Used by selfplay.make_1v1_pistol_flame as the bootstrap.

  stage 2  make_1v1_stage2_movement_obstacles
           1v1 pistol+flame, random spawn cells, 5-12 random
           obstacles per fight. Each (seed) produces a different
           layout so the V network must generalize across maps.

  stage 3  make_4v4_team       (TODO)
  stage 4  make_real_loadouts  (TODO)
"""
from __future__ import annotations

import os
import sys
from typing import Optional

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

import leekwars_c._engine as _v2
import test_v2_summon as base


# ============================================================ catalogues ==

WEAPON_PISTOL = 37
CHIP_FLAME    = 5

# Real obstacle template ids from upstream Java's ObstacleInfo
# registry. Anything outside this list silently fails to register
# with the engine -- which is why the earlier "1..8" pool didn't
# block movement.
_SMALL_OBSTACLES = (
    5, 20, 21, 22, 31, 32, 38, 40, 41, 42, 48,
    50, 53, 55, 57, 59, 62, 63, 66,
)
_LARGE_OBSTACLES = (
    11, 17, 18, 34, 43, 44, 45, 46, 47, 49,
    52, 54, 56, 58, 61, 64, 65,
)

# Map dimensions (matches the engine's default 18x18 diamond).
_MAP_W   = 18
_MAP_MOD = _MAP_W * 2 - 1                       # 35
_NB_CELLS = _MAP_MOD * _MAP_W - (_MAP_W - 1)    # 613


# ============================================================== helpers ==


def _cell_pixel(cell_id: int) -> tuple[int, int]:
    """Compute leek-wars-client's rendering pixel coords for ``cell_id``.
    Used to pick spawn cells from designated half-map zones."""
    px = (cell_id % _MAP_MOD) * 2
    py = (cell_id // _MAP_MOD) * 2
    if px > _MAP_MOD:
        px = px % _MAP_MOD
        py += 1
    return px, py


def _spawn_pools() -> tuple[list[int], list[int]]:
    """Build two lists of valid spawn cells -- one for each side of
    the map -- by sweeping every cell id and bucketing by pixel x.
    The pools are computed once and reused; cell layout is static
    per map.

    Left zone: pixel x in [4, 10], inner band of py.
    Right zone: pixel x in [24, 30], inner band of py.
    """
    left, right = [], []
    for cid in range(_NB_CELLS):
        px, py = _cell_pixel(cid)
        if 6 <= py <= 28:
            if 4 <= px <= 10:
                left.append(cid)
            elif 24 <= px <= 30:
                right.append(cid)
    return left, right


_SPAWN_LEFT, _SPAWN_RIGHT = _spawn_pools()


def _generate_obstacles(rng: np.random.Generator,
                         reserved: set[int],
                         min_count: int = 5,
                         max_count: int = 12) -> dict[int, int]:
    """Place a random number of obstacles in the diamond's central
    band, avoiding the reserved cells (typically the two spawns).
    Mirrors the Java `Map.generateMap` shape (random cell, random
    size, random type) with the simplifying choice of staying inside
    the central rectangle so we don't generate degenerate corners.

    Returns ``{cell_id: template_id}`` ready for
    ``Engine.set_custom_map(obstacles=...)``.
    """
    n = int(rng.integers(min_count, max_count + 1))
    used: set[int] = set(reserved)
    out: dict[int, int] = {}

    # Candidate pool: interior of the diamond (px, py in [6, 28]).
    candidates: list[int] = []
    for cid in range(_NB_CELLS):
        if cid in used: continue
        px, py = _cell_pixel(cid)
        if 6 <= px <= 28 and 6 <= py <= 28:
            candidates.append(cid)

    rng.shuffle(candidates)

    placed = 0
    for cid in candidates:
        if placed >= n: break
        if cid in used: continue
        # 30% chance of large 2x2; need E, S, SE neighbors free.
        large = rng.random() < 0.3
        if large:
            e  = cid + _MAP_W            # +18 (Pathfinding.EAST)
            s  = cid + _MAP_W - 1        # +17 (Pathfinding.SOUTH)
            se = s + _MAP_W              # +35
            if all(0 <= c < _NB_CELLS and c not in used for c in (e, s, se)):
                tmpl = int(rng.choice(_LARGE_OBSTACLES))
                out[cid] = tmpl
                used.update({cid, e, s, se})
                placed += 1
                continue
        # Fallback: small.
        tmpl = int(rng.choice(_SMALL_OBSTACLES))
        out[cid] = tmpl
        used.add(cid)
        placed += 1
    return out


# ====================================================== weapon registries ==


def _register_pistol_and_chips(eng: _v2.Engine) -> None:
    raw = base.WEAPONS[WEAPON_PISTOL]
    eng.add_weapon(item_id=WEAPON_PISTOL, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]],
                    passive_effects=[])
    base.register_all_chips_v2(eng)


# ============================================================== stage 1 ==


def make_1v1_stage1_static(seed: int = 1, *,
                             life: int = 800, max_turns: int = 30) -> _v2.Engine:
    """Bootstrap scenario: fixed spawn (72 vs 144), no obstacles."""
    eng = _v2.Engine()
    _register_pistol_and_chips(eng)
    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_entity(team=0, fid=1, name="A", level=100, life=life, tp=14, mp=6,
                    strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                    science=0, magic=0, cores=10, ram=10, farmer=1, team_id=1,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], cell=72)
    eng.add_entity(team=1, fid=2, name="B", level=100, life=life, tp=14, mp=6,
                    strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                    science=0, magic=0, cores=10, ram=10, farmer=2, team_id=2,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], cell=144)
    eng.set_seed(seed); eng.set_max_turns(max_turns); eng.set_type(1)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144], map_id=1)
    return eng


# ============================================================== stage 2 ==


def make_1v1_stage2_movement_obstacles(seed: int = 1, *,
                                         life: int = 1500,
                                         max_turns: int = 35,
                                         min_obstacles: int = 5,
                                         max_obstacles: int = 12) -> _v2.Engine:
    """Stage-2 scenario: 1v1 pistol+flame with movement on a map
    where every (seed) draws its own:

      * spawn cell for each leek (uniform from a left / right pool)
      * obstacle count in [min_obstacles, max_obstacles]
      * obstacle layout (cells + size 1/2 + Java template ids)

    The diversity forces the V network to generalize over LoS,
    detours, and different starting distances rather than memorize
    a single map.

    Defaults:
      life=1500       -- intermediate between stage-1 (800) and
                         real loadouts (~3000+). Keeps fights
                         decisive within ~30 turns.
      max_turns=35    -- a few extra turns to navigate around
                         obstacles before engaging.
    """
    rng = np.random.default_rng(seed)
    cell_a = int(rng.choice(_SPAWN_LEFT))
    cell_b = int(rng.choice(_SPAWN_RIGHT))
    obstacles = _generate_obstacles(
        rng, reserved={cell_a, cell_b},
        min_count=min_obstacles, max_count=max_obstacles)

    eng = _v2.Engine()
    _register_pistol_and_chips(eng)
    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_entity(team=0, fid=1, name="A", level=100, life=life, tp=14, mp=6,
                    strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                    science=0, magic=0, cores=10, ram=10, farmer=1, team_id=1,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], cell=cell_a)
    eng.add_entity(team=1, fid=2, name="B", level=100, life=life, tp=14, mp=6,
                    strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                    science=0, magic=0, cores=10, ram=10, farmer=2, team_id=2,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], cell=cell_b)
    eng.set_seed(seed); eng.set_max_turns(max_turns); eng.set_type(1)
    eng.set_custom_map(obstacles=obstacles, team1=[cell_a], team2=[cell_b], map_id=1)
    return eng


# ============================================================ smoke test ==

if __name__ == "__main__":
    # Visualize the layout diversity over a few seeds. We sample
    # the obstacle dict directly via the same RNG path the scenario
    # uses, since entity_cell() can't be queried before run().
    for s in range(5):
        rng = np.random.default_rng(s)
        ca = int(rng.choice(_SPAWN_LEFT))
        cb = int(rng.choice(_SPAWN_RIGHT))
        obs = _generate_obstacles(rng, {ca, cb})
        print(f"  seed {s}: spawn=({ca:>3}, {cb:>3})  obstacles={len(obs)}")
    # Also confirm the engine accepts the obstacles -- run a dummy
    # fight with a no-op AI.
    eng = make_1v1_stage2_movement_obstacles(seed=42)
    eng.set_ai_callback(lambda i, t: 0)
    eng.run()
    print(f"  seed 42 dummy fight: winner={eng.winner}, "
          f"actions={len(eng.stream_dump())}, "
          f"final cells: A={eng.entity_cell(0)} B={eng.entity_cell(1)}")
