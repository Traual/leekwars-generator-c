"""Run one fight with a trained Leek-Zero NN on both sides and dump
``report.json`` for the leek-wars-client viewer.

Usage:
    python bindings/python/leekzero/replay_one.py \
        --checkpoint leekzero_runs/stage2_hybrid_v3/best.pt \
        --seed 42

Then open http://localhost:8080/fight/local in the browser (assumes
the replay server set up earlier in the session is running).
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

from leekwars_c.replay import build_leek, build_report, write_report
from leekzero.model import MLPv1, HybridV1
from leekzero import agent as agent_mod
from leekzero.scenarios import (
    make_1v1_stage2_movement_obstacles,
    make_1v1_stage1_static,
    WEAPON_PISTOL, CHIP_FLAME,
)


DEFAULT_REPORT = (
    "C:/Users/aurel/Desktop/Training Weights Leekwars/"
    "Leekwars-Tools/leek-wars/public/static/report.json"
)


def load_model(path: str, model_kind: str):
    if model_kind == "hybrid":
        m = HybridV1()
    else:
        m = MLPv1()
    if path and os.path.isfile(path):
        m.load_state_dict(torch.load(path, map_location="cpu", weights_only=True))
        print(f"  loaded {path}")
    else:
        print(f"  WARNING: no checkpoint at {path}, using random init")
    return m.eval()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--checkpoint",
                    default="leekzero_runs/stage2_hybrid_v3/best.pt")
    p.add_argument("--model", default="hybrid", choices=["mlp", "hybrid"])
    p.add_argument("--scenario", default="stage2",
                    choices=["stage1", "stage2"])
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--temperature", type=float, default=0.1,
                    help="agent softmax temperature; 0 = pure argmax. "
                         "Low = competitive / deterministic play. "
                         "Higher = more exploration / variety.")
    p.add_argument("--out", default=DEFAULT_REPORT)
    args = p.parse_args()

    print(f"loading model {args.model} from {args.checkpoint}")
    model = load_model(args.checkpoint, args.model)

    AgentCls = (agent_mod.HybridGreedyAgent if args.model == "hybrid"
                  else agent_mod.GreedyVAgent)

    print(f"running 1 fight: {args.scenario}, seed={args.seed}, "
          f"NN(team A) vs NN(team B), temperature={args.temperature}")

    if args.scenario == "stage2":
        eng = make_1v1_stage2_movement_obstacles(seed=args.seed)
    else:
        eng = make_1v1_stage1_static(seed=args.seed)

    agent_a = AgentCls(model, weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
                       temperature=args.temperature, epsilon=0.0,
                       rng=np.random.default_rng(args.seed))
    agent_b = AgentCls(model, weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
                       temperature=args.temperature, epsilon=0.0,
                       rng=np.random.default_rng(args.seed + 999_999))

    agent_mod._callback_engine = eng

    def cb(idx: int, turn: int) -> int:
        team = eng.entity_team(idx)
        return (agent_a if team == 0 else agent_b).play_turn(eng, idx, turn)

    eng.set_ai_callback(cb)
    eng.run()

    # Pull the obstacles back from the scenario so the viewer renders
    # them. Stage-2 generates them deterministically from the seed; we
    # re-run that helper to recover the dict.
    obs = {}
    if args.scenario == "stage2":
        from leekzero.scenarios import _generate_obstacles, _SPAWN_LEFT, _SPAWN_RIGHT
        rng = np.random.default_rng(args.seed)
        cell_a = int(rng.choice(_SPAWN_LEFT))
        cell_b = int(rng.choice(_SPAWN_RIGHT))
        obs = _generate_obstacles(rng, reserved={cell_a, cell_b})
        # Convert to client format (replay_top.py has the helper).
        from replay_top import _obstacles_to_client_format
        obs = _obstacles_to_client_format(obs)

    # Build leek dicts. The agent_a is on team 0 (= client team 1),
    # agent_b is on team 1 (= client team 2).
    cell_a = eng.entity_cell(0)
    cell_b = eng.entity_cell(1)
    # Engine has already advanced cells during the fight; we want
    # the INITIAL positions for build_leek. Re-compute from the
    # scenario seed.
    if args.scenario == "stage2":
        rng = np.random.default_rng(args.seed)
        init_a = int(rng.choice(_SPAWN_LEFT))
        init_b = int(rng.choice(_SPAWN_RIGHT))
    else:
        init_a, init_b = 72, 144

    leeks = [
        build_leek(id=0, team=1, name="NN_A", cell=init_a,
                    life=1500 if args.scenario == "stage2" else 800,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], farmer=1),
        build_leek(id=1, team=2, name="NN_B", cell=init_b,
                    life=1500 if args.scenario == "stage2" else 800,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], farmer=2),
    ]
    report = build_report(eng, leeks, team1_ids=[0], team2_ids=[1],
                            obstacles=obs)
    write_report(args.out, report)

    n_actions = len(report["fight"]["actions"])
    winner = int(eng.winner)
    print()
    print(f"  wrote {args.out}")
    print(f"  actions:  {n_actions}")
    print(f"  winner:   {winner} (-1=draw, 0=team A, 1=team B)")
    print(f"  view at:  http://localhost:8080/fight/local")


if __name__ == "__main__":
    main()
