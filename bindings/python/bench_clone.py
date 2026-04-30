"""Benchmark: state clone + legal_actions enumeration in C vs Python.

Builds a synthetic 5x5 topology in both engines, populates one
entity, then times clone() and legal_actions() over many iterations.
"""
from __future__ import annotations

import os
import sys
import time

# Make leekwars_c importable from this script
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import leekwars_c as lwc


def make_5x5_topo():
    """Build a 5x5 grid topology compatible with the Topology.from_grid API."""
    cells = []
    neighbors = []
    for y in range(5):
        for x in range(5):
            cid = y * 5 + x
            cells.append((cid, x, y, True))
            s = (y + 1) * 5 + x if y + 1 < 5 else -1
            w = y * 5 + (x - 1) if x - 1 >= 0 else -1
            n = (y - 1) * 5 + x if y - 1 >= 0 else -1
            e = y * 5 + (x + 1) if x + 1 < 5 else -1
            neighbors.append((s, w, n, e))
    return lwc._engine.Topology.from_grid(5, 5, cells, neighbors)


def make_state():
    s = lwc.State()
    topo = make_5x5_topo()
    s.set_topology(topo)
    s.add_entity(fid=1, team_id=0, cell_id=0,
                 total_tp=10, total_mp=4,
                 hp=1000, total_hp=1000,
                 weapons=[37, 60], chips=[1])
    s.add_entity(fid=2, team_id=1, cell_id=12,
                 total_tp=10, total_mp=4,
                 hp=1000, total_hp=1000,
                 weapons=[37], chips=[])
    return s


def make_profile():
    p = lwc.InventoryProfile()
    p.set_weapon(0, cost=5, min_range=2, max_range=5, launch_type=7, needs_los=0)
    p.set_weapon(1, cost=4, min_range=1, max_range=1, launch_type=7, needs_los=0)
    p.set_chip(0, cost=4, min_range=1, max_range=6, launch_type=7, needs_los=0)
    return p


def bench_clone(n=100000):
    s = make_state()
    t0 = time.perf_counter()
    for _ in range(n):
        c = s.clone()
    dt = time.perf_counter() - t0
    print(f"  Clone (C, memcpy):   {1e6 * dt / n:.2f} us/clone "
          f"({n} iter in {dt:.2f}s)")


def bench_legal(n=10000):
    s = make_state()
    p = make_profile()
    t0 = time.perf_counter()
    for _ in range(n):
        actions = s.legal_actions(0, p)
    dt = time.perf_counter() - t0
    print(f"  legal_actions (C):   {1e6 * dt / n:.2f} us/call "
          f"({n} iter in {dt:.2f}s, {len(actions)} actions/call)")


def bench_features(n=100000):
    import numpy as np
    s = make_state()
    buf = np.zeros(256, dtype=np.float32)
    t0 = time.perf_counter()
    for _ in range(n):
        s.extract_mlp_features(0, buf)
    dt = time.perf_counter() - t0
    print(f"  extract_mlp (C):     {1e6 * dt / n:.2f} us/call "
          f"({n} iter in {dt:.2f}s)")
    # show a few values to confirm the buffer was actually filled
    nz = int((buf != 0).sum())
    print(f"     ({nz}/256 non-zero, first non-zero values: {buf[buf != 0][:5]})")


def main():
    print("Smoke test:")
    s = make_state()
    p = make_profile()
    print(f"  state.n_entities = {s.n_entities}")
    actions = s.legal_actions(0, p)
    print(f"  {len(actions)} legal actions for entity 0")
    by_type = {}
    for a in actions:
        by_type.setdefault(a.type, 0)
        by_type[a.type] += 1
    print(f"  breakdown by type: {by_type}")

    print("\nDamage smoke test (USE_WEAPON):")
    s = make_state()
    print(f"  before: e0.hp={s.entity_hp(0)}  e1.hp={s.entity_hp(1)}")
    fire = lwc.Action(type=lwc.ActionType.USE_WEAPON, target_cell_id=12)
    ok = s.apply_action(0, fire)
    print(f"  applied: {ok}  e1.hp={s.entity_hp(1)}  e1.alive={s.entity_alive(1)}")
    # Hit until dead
    n_hits = 1
    while s.entity_alive(1) and n_hits < 50:
        s.apply_action(0, fire)
        n_hits += 1
        # Reset TP so we can keep firing
        s._reset_tp_for_test() if hasattr(s, "_reset_tp_for_test") else None
    print(f"  after {n_hits} hits: e1.hp={s.entity_hp(1)}  e1.alive={s.entity_alive(1)}")

    print("\nBenchmarks:")
    bench_clone()
    bench_legal()
    bench_features()


if __name__ == "__main__":
    main()
