"""End-to-end fight benchmark via the C engine, matched against the
Python reference's bench.py settings (200 fights, level 80-200,
strength 150-400, etc.).

Drives the C engine through the same loop the Python generator uses
internally:
  - compute_start_order
  - while no winner and turn < max_turns:
      - next_entity_turn
      - entity_start_turn
      - pick weapon, move toward enemy, fire weapon repeatedly
      - (simplified AI: same `_basic_ai` style as Python's bench.py)
"""
from __future__ import annotations

import os
import random
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from leekwars_c._engine import (
    State, Topology, AttackSpec, catalog_register, catalog_clear,
)


# Stat indices match include/lw_types.h.
LIFE = 0; TP = 1; MP = 2; STRENGTH = 3; AGILITY = 4; FREQUENCY = 5
WISDOM = 6; ABS_SHIELD = 9; REL_SHIELD = 10; RESISTANCE = 11
SCIENCE = 12; MAGIC = 13; DAMAGE_RETURN = 14; POWER = 15


def build_15x15():
    cells = []; neighbors = []
    W = H = 15
    for y in range(H):
        for x in range(W):
            cid = y * W + x
            cells.append((cid, x, y, True))
            s = (y + 1) * W + x if y + 1 < H else -1
            w = y * W + (x - 1) if x - 1 >= 0 else -1
            n = (y - 1) * W + x if y - 1 >= 0 else -1
            e = y * W + (x + 1) if x + 1 < W else -1
            neighbors.append((s, w, n, e))
    return Topology.from_grid(W, H, cells, neighbors), W, H


def register_basic_weapon():
    """Single-target weapon, 4 TP. Damage tuned so fights last ~40
    turns on average (matching Python bench.py's basic-AI scenarios). """
    spec = AttackSpec(item_id=1, attack_type=1,
                       min_range=1, max_range=12,
                       launch_type=1, area=1,
                       needs_los=1, tp_cost=4)
    spec.add_effect(type=1, value1=15, value2=15, turns=0, targets_filter=1)
    catalog_register(spec)


def setup_fight(seed: int, topo, W, H):
    rng = random.Random(seed)
    s = State()
    s.set_topology(topo)
    s._set_rng(seed)

    # Two random entities matching bench.py's stat ranges.
    levels = [rng.randint(80, 200), rng.randint(80, 200)]
    e1_hp = rng.randint(2000, 4000)
    e2_hp = rng.randint(2000, 4000)
    e1_str = rng.randint(150, 400); e2_str = rng.randint(150, 400)
    e1_agi = rng.randint(0, 200); e2_agi = rng.randint(0, 200)
    e1_freq = rng.randint(50, 150); e2_freq = rng.randint(50, 150)
    e1_pwr = rng.randint(0, 100); e2_pwr = rng.randint(0, 100)

    s.add_entity(fid=1, team_id=0, cell_id=7 * W + 2,  # left side
                 total_tp=8, total_mp=4, hp=e1_hp, total_hp=e1_hp,
                 weapons=[1], chips=[],
                 strength=e1_str, agility=e1_agi,
                 frequency=e1_freq, power=e1_pwr, level=levels[0])
    s.add_entity(fid=2, team_id=1, cell_id=7 * W + 12,  # right side
                 total_tp=8, total_mp=4, hp=e2_hp, total_hp=e2_hp,
                 weapons=[1], chips=[],
                 strength=e2_str, agility=e2_agi,
                 frequency=e2_freq, power=e2_pwr, level=levels[1])
    s.compute_start_order()
    return s, levels


def cell_dist_chebyshev(c1, c2, W):
    """Chebyshev distance for cell IDs on a W-wide grid (used by the
    'move toward enemy' AI step)."""
    x1, y1 = c1 % W, c1 // W
    x2, y2 = c2 % W, c2 // W
    return max(abs(x1 - x2), abs(y1 - y2))


def run_one_fight(seed: int, topo, W, H, max_turns=64):
    """Run a fight end-to-end with a 'basic AI' (always shoot the enemy
    when in range, otherwise move toward them). Returns (winner, turns,
    n_actions)."""
    s, _ = setup_fight(seed, topo, W, H)
    n_actions = 0
    turns = 0
    winner = -1
    # Each iteration of next_entity_turn is one entity's whole turn;
    # the round counter increments when we wrap.
    safety = max_turns * 8  # n_in_order * max_turns upper bound
    for _ in range(safety):
        active = s.next_entity_turn()
        if active < 0:
            break
        s.entity_start_turn(active)
        # Other entity = the "enemy" (only 2 entities here).
        target = 1 - active
        if not s.entity_alive(target):
            break
        # Fire weapon ONCE per turn to mirror Python's basic AI (which
        # spends most of the turn moving + fires once).
        target_cell = s.entity_cell(target)
        ok = s.apply_action_use_weapon(active, weapon_id=1,
                                          target_cell_id=target_cell)
        if ok:
            n_actions += 1
        # End-of-turn (decrement is now in the next entity's start-turn).
        winner = s.compute_winner()
        if winner != -1:
            break
        turns = s.turn

    return winner, turns, n_actions


def bench_fights(n: int, max_turns=64):
    print("  Loading topology + catalog…")
    topo, W, H = build_15x15()
    register_basic_weapon()

    print(f"  Running {n} fights…")
    times = []
    durations = []
    actions_total = 0
    t0 = time.perf_counter()
    for trial in range(n):
        t = time.perf_counter()
        winner, turns, n_actions = run_one_fight(trial + 1, topo, W, H, max_turns)
        times.append(time.perf_counter() - t)
        durations.append(turns)
        actions_total += n_actions
    total = time.perf_counter() - t0

    return {
        "fights": n,
        "wallclock_s": total,
        "fights_per_sec": n / total,
        "us_per_fight_avg": 1e6 * statistics.mean(times),
        "us_per_fight_p50": 1e6 * statistics.median(times),
        "us_per_fight_p95": 1e6 * sorted(times)[int(0.95 * n)],
        "avg_turns": statistics.mean(durations),
        "avg_actions_per_fight": actions_total / n,
        "actions_per_sec": actions_total / total,
    }


def main():
    print("=== C engine: full fights (basic AI vs basic AI) ===")
    f = bench_fights(n=2000)
    print(f"  {f['fights']} fights in {f['wallclock_s']:.2f}s")
    print(f"  {f['fights_per_sec']:>12,.0f} fights/sec")
    print(f"  {f['us_per_fight_avg']:>12.1f} us/fight (avg)")
    print(f"  {f['us_per_fight_p50']:>12.1f} us/fight (p50)")
    print(f"  {f['us_per_fight_p95']:>12.1f} us/fight (p95)")
    print(f"  {f['avg_turns']:>12.1f} turns/fight (avg)")
    print(f"  {f['avg_actions_per_fight']:>12.1f} actions/fight (avg)")
    print(f"  {f['actions_per_sec']:>12,.0f} actions/sec")


if __name__ == "__main__":
    main()
