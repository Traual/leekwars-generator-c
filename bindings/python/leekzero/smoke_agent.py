"""Smoke-test the GreedyVAgent end-to-end on a tiny 1v1.

Validates:
  * Agent produces actions inside the AI callback (no crash).
  * Fight ends in finite turns.
  * Per-turn time budget is reasonable for a 24-48h training run
    (target <= a few hundred ms / turn at depth 1).

This is NOT a parity test -- the agent has random init weights
so V is meaningless. We're just checking the pipeline.
"""
from __future__ import annotations

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

import numpy as np
import torch

import leekwars_c._engine as _v2
import test_v2_summon as base   # WEAPONS / CHIPS catalogues + register helpers

from leekzero.model import MLPv1
from leekzero import agent as agent_mod


WEAPON_ID = 37    # pistol
CHIP_ID   = 5     # flame


def setup_1v1():
    eng = _v2.Engine()
    raw = base.WEAPONS[WEAPON_ID]
    eng.add_weapon(item_id=WEAPON_ID, name=raw["name"], cost=int(raw["cost"]),
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
                    science=0, magic=0, cores=10, ram=10, farmer=1, team_id=1,
                    weapons=[WEAPON_ID], chips=[CHIP_ID], cell=72)
    eng.add_entity(team=1, fid=2, name="B", level=100, life=2500, tp=14, mp=6,
                    strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                    science=0, magic=0, cores=10, ram=10, farmer=2, team_id=2,
                    weapons=[WEAPON_ID], chips=[CHIP_ID], cell=144)
    eng.set_seed(1); eng.set_max_turns(15); eng.set_type(1)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144], map_id=1)
    return eng


def main():
    print("Building model + agent...")
    torch.manual_seed(0)
    model = MLPv1()
    # epsilon=0.6 forces real combat actions despite the V being
    # random init. This is the kind of exploration ramp the early
    # phase of self-play needs to bootstrap a coherent (state,
    # outcome) dataset.
    a = agent_mod.GreedyVAgent(
        model, weapons=[WEAPON_ID], chips=[CHIP_ID],
        max_actions_per_turn=20,
        temperature=0.5, epsilon=0.6)

    print("Running 1v1 fight with random-init V + eps=0.6 exploration...")
    eng = setup_1v1()

    # Wire the agent to the engine.
    agent_mod._callback_engine = eng
    n_decisions_total = 0
    turn_times: list[float] = []

    def cb(idx: int, turn: int) -> int:
        nonlocal n_decisions_total
        t0 = time.perf_counter()
        n = a.play_turn(eng, idx, turn)
        turn_times.append(time.perf_counter() - t0)
        n_decisions_total += n
        return n

    eng.set_ai_callback(cb)
    fight_t0 = time.perf_counter()
    eng.run()
    fight_dt = time.perf_counter() - fight_t0

    print()
    print(f"  fight time:        {fight_dt*1000:.0f} ms")
    print(f"  total decisions:   {n_decisions_total}")
    print(f"  per-turn (mean):   {1000*np.mean(turn_times):.1f} ms "
          f"(median {1000*np.median(turn_times):.1f}, "
          f"max {1000*np.max(turn_times):.1f})")
    print(f"  winner team:       {eng.winner}")

    n_actions = len(eng.stream_dump())
    print(f"  action stream:     {n_actions} entries")

    # Sanity: stream should contain at least START + END FIGHT and
    # several USE_WEAPON / USE_CHIP / END_TURN actions.
    types = {}
    for ev in eng.stream_dump():
        types.setdefault(ev["type"], 0)
        types[ev["type"]] += 1
    print(f"  action types:      {dict(sorted(types.items()))}")


if __name__ == "__main__":
    main()
