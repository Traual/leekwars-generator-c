"""Self-play smoke test in Python via the C engine.

Builds a 1v1 fight from Python, populates the catalog, and drives the
fight loop end-to-end with a hard-coded "always shoot" policy. This
proves the Cython bindings expose enough of the new C primitives to
run AlphaZero-quality self-play with NN-based action selection (the
real AI just replaces the policy).
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import leekwars_c as lwc
from leekwars_c._engine import (
    AttackSpec,
    State,
    catalog_clear,
    catalog_register,
    catalog_size,
    WIN_DRAW,
    WIN_ONGOING,
)


# Match Effect.TYPE_* constants from lw_effect.h.
EFFECT_DAMAGE = 1
TARGET_ENEMIES = 1


def make_15x15_topo():
    cells = []
    neighbors = []
    for y in range(15):
        for x in range(15):
            cid = y * 15 + x
            cells.append((cid, x, y, True))
            s = (y + 1) * 15 + x if y + 1 < 15 else -1
            w = y * 15 + (x - 1) if x - 1 >= 0 else -1
            n = (y - 1) * 15 + x if y - 1 >= 0 else -1
            e = y * 15 + (x + 1) if x + 1 < 15 else -1
            neighbors.append((s, w, n, e))
    return lwc._engine.Topology.from_grid(15, 15, cells, neighbors)


def populate_catalog():
    catalog_clear()
    # A weapon with 4 TP, range 1-12, single-cell, deals 80 + 40*jet
    # damage scaled by strength. Item id = 1.
    weapon = AttackSpec(
        item_id=1,
        attack_type=1,
        min_range=1, max_range=12,
        launch_type=1,
        area=1,           # SINGLE_CELL
        needs_los=1,
        tp_cost=4,
    )
    weapon.add_effect(
        type=EFFECT_DAMAGE,
        value1=80, value2=40,
        targets_filter=TARGET_ENEMIES,
    )
    catalog_register(weapon)


def make_state(seed: int = 42) -> State:
    s = State()
    s.set_topology(make_15x15_topo())
    # Caster at (3, 7), Target at (10, 7).
    s.add_entity(
        fid=1, team_id=0, cell_id=7 * 15 + 3,
        total_tp=8, total_mp=0,
        hp=800, total_hp=800,
        weapons=[1], chips=[],
        strength=80, frequency=100,
    )
    s.add_entity(
        fid=2, team_id=1, cell_id=7 * 15 + 10,
        total_tp=8, total_mp=0,
        hp=800, total_hp=800,
        weapons=[1], chips=[],
        strength=80, frequency=101,
    )
    s.set_seed(seed)
    return s


def shoot_policy(state: State, active: int) -> int:
    """Always-shoot policy: spend TP firing weapon 1 at the other entity.
    Returns the number of shots fired."""
    target = 1 if active == 0 else 0
    if not state.entity_alive(target):
        return 0
    target_cell = state.entity_cell(target)
    n_shots = 0
    while True:
        avail_tp = (state.entity_used_tp(active),
                    state._s_total_tp(active) if hasattr(state, "_s_total_tp")
                    else 8 - state.entity_used_tp(active))
        # Just try until apply_action returns False (out of TP).
        action = lwc.Action(
            type=lwc.ActionType.USE_WEAPON,
            weapon_id=1,
            target_cell_id=target_cell,
        )
        if not state.entity_alive(target):
            break
        if state.entity_used_tp(active) + 4 > 8:  # weapon cost = 4, total = 8
            break
        ok = state.apply_action(active, action)
        if not ok:
            break
        n_shots += 1
        if n_shots > 5:  # safety
            break
    return n_shots


def run_one_fight(seed: int) -> tuple[int, int, int]:
    """Returns (winner, turns, total_shots)."""
    state = make_state(seed)
    state.compute_start_order()
    total_shots = 0
    turns = 0
    while turns < 50:
        active = state.next_entity_turn()
        if active < 0:
            break
        state.entity_start_turn(active)
        total_shots += shoot_policy(state, active)
        state.entity_end_turn(active)
        if state.compute_winner() != WIN_ONGOING:
            break
        turns += 1
    return state.compute_winner(), turns, total_shots


def main():
    populate_catalog()
    print(f"catalog_size = {catalog_size()}")

    # Smoke: a single fight with seed=42.
    winner, turns, shots = run_one_fight(42)
    print(f"single fight seed=42: winner=team{winner} turns={turns} shots={shots}")
    assert winner in (0, 1), f"expected a clean winner, got {winner}"

    # Bench: 1000 fights, count outcomes.
    n = 1000
    t0 = time.perf_counter()
    wins = [0, 0, 0]  # team 0, team 1, draw
    for i in range(n):
        w, _, _ = run_one_fight(i + 1)
        if   w == 0:           wins[0] += 1
        elif w == 1:           wins[1] += 1
        else:                  wins[2] += 1
    dt = time.perf_counter() - t0
    print(f"\n{n} fights in {dt*1000:.1f} ms -> {dt*1e6/n:.0f} us/fight")
    print(f"team0 wins: {wins[0]}, team1 wins: {wins[1]}, draws: {wins[2]}")

    print("\nSelf-play smoke OK.")


if __name__ == "__main__":
    main()
