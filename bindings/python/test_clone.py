"""Smoke test for Engine.clone().

Validates that cloning a post-run engine produces a state with the
same observable fields (entity count, hp, cell, winner). Also runs a
small benchmark to gauge clone() throughput for MCTS feasibility.
"""
from __future__ import annotations

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

import leekwars_c._engine as _v2
import test_v2_summon as base


def setup_simple_1v1():
    """Build a 1v1 pistol+flame fight with two leeks."""
    eng = _v2.Engine()
    raw = base.WEAPONS[37]  # pistol
    eng.add_weapon(item_id=37, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]],
                    passive_effects=[])
    base.register_all_chips_v2(eng)
    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_entity(team=0, fid=1, name="A", level=100, life=2500, tp=14, mp=6,
                   strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                   science=0, magic=0, cores=10, ram=10,
                   farmer=1, team_id=1, weapons=[37], chips=[5], cell=72)
    eng.add_entity(team=1, fid=2, name="B", level=100, life=2500, tp=14, mp=6,
                   strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                   science=0, magic=0, cores=10, ram=10,
                   farmer=2, team_id=2, weapons=[37], chips=[5], cell=144)
    eng.set_seed(1); eng.set_max_turns(8); eng.set_type(1)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144], map_id=1)

    def my_ai(idx, turn):
        my_cell = eng.entity_cell(idx)
        other_cell = eng.entity_cell(1 - idx)
        eng.move_toward(idx, other_cell, max_mp=6)
        for _ in range(8):
            other_cell = eng.entity_cell(1 - idx)
            rc = eng.fire_weapon(idx, 37, other_cell)
            if rc <= 0: break
        return 0

    eng.set_ai_callback(my_ai)
    return eng


def test_clone_basic():
    """Run a fight, clone post-run, verify clone state equals original."""
    print("=== test_clone_basic ===")
    eng = setup_simple_1v1()
    eng.run()
    print(f"  original: n_entities={eng.n_entities()}, "
          f"e0.hp={eng.entity_hp(0)}, e1.hp={eng.entity_hp(1)}, "
          f"e0.cell={eng.entity_cell(0)}, e1.cell={eng.entity_cell(1)}, "
          f"winner={eng.winner}")

    # Clone after run.
    clone = eng.clone()
    print(f"  clone:    n_entities={clone.n_entities()}, "
          f"e0.hp={clone.entity_hp(0)}, e1.hp={clone.entity_hp(1)}, "
          f"e0.cell={clone.entity_cell(0)}, e1.cell={clone.entity_cell(1)}, "
          f"winner={clone.winner}")

    # All observable fields should match.
    assert eng.n_entities() == clone.n_entities()
    for i in range(eng.n_entities()):
        assert eng.entity_hp(i) == clone.entity_hp(i), f"hp mismatch at idx {i}"
        assert eng.entity_cell(i) == clone.entity_cell(i), f"cell mismatch at idx {i}"
        assert eng.entity_alive(i) == clone.entity_alive(i), f"alive mismatch at idx {i}"
        assert eng.entity_team(i) == clone.entity_team(i)
        assert eng.entity_fid(i) == clone.entity_fid(i)
    assert eng.winner == clone.winner
    print("  OK: all entity/team fields match")


def test_clone_isolation():
    """Mutate the clone, verify original is unchanged."""
    print("\n=== test_clone_isolation ===")
    eng = setup_simple_1v1()
    eng.run()

    # Snapshot original observables.
    orig_hps  = [eng.entity_hp(i)   for i in range(eng.n_entities())]
    orig_cell = [eng.entity_cell(i) for i in range(eng.n_entities())]

    # Clone, then apply some atomic actions on the clone (move + fire).
    clone = eng.clone()
    # Use the clone's primitives directly. Fire weapon on entity 0
    # against entity 1's cell -- might or might not succeed depending
    # on TP / state, but should NOT affect the original engine.
    target_cell = clone.entity_cell(1)
    for _ in range(3):
        clone.fire_weapon(0, 37, target_cell)

    # Original must still match the snapshot.
    for i in range(eng.n_entities()):
        assert eng.entity_hp(i)   == orig_hps[i],  f"original hp drifted at idx {i}"
        assert eng.entity_cell(i) == orig_cell[i], f"original cell drifted at idx {i}"
    print("  OK: original unchanged after clone mutation")


def bench_clone(n=10000):
    print(f"\n=== bench_clone n={n} ===")
    eng = setup_simple_1v1()
    eng.run()
    # Warm-up
    for _ in range(100):
        c = eng.clone()
    # Time
    t0 = time.perf_counter()
    for _ in range(n):
        c = eng.clone()
    dt = time.perf_counter() - t0
    print(f"  {n} clones in {dt*1000:.1f} ms = {1e6 * dt / n:.2f} us/clone")


if __name__ == "__main__":
    test_clone_basic()
    test_clone_isolation()
    bench_clone()
