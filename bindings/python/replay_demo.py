"""Run a sample 1v1 fight and dump it as report.json so we can watch it
in the leek-wars-client (https://github.com/leek-wars/leek-wars-client).

Usage:
    python replay_demo.py [--out path/to/report.json]

Default output path points at the public/static/ folder of the local
leek-wars-client checkout in Leekwars-Tools/leek-wars/.

After running this script, in another terminal:
    cd "<...>/Leekwars-Tools/leek-wars"
    npm run dev          # vite dev server, usually localhost:5173
Then open http://localhost:5173/fight/local in the browser.
"""
from __future__ import annotations

import argparse
import os
import sys

# Make the v2 binding importable when run from this directory.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

import leekwars_c._engine as _v2
from leekwars_c.replay import build_leek, build_report, write_report

# Use the upstream JSON catalogues for accurate weapon / chip stats.
import test_v2_summon as base   # gives WEAPONS / CHIPS / register_all_chips_v2

WEAPON_ID = 37    # pistol
CHIP_ID   = 5     # flame


def run_demo_fight(seed: int = 1, max_turns: int = 20):
    """1v1 with both leeks moving toward each other and firing pistol + flame."""
    eng = _v2.Engine()

    # Weapon
    raw = base.WEAPONS[WEAPON_ID]
    eng.add_weapon(item_id=WEAPON_ID, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]],
                    passive_effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                                       int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                                      for e in raw.get("passive_effects", [])])

    # Register every chip so the binding can resolve CHIP_ID.
    base.register_all_chips_v2(eng)

    # Two farmers, two teams, two leeks at opposite cells.
    eng.add_farmer(1, "Alice", "fr"); eng.add_farmer(2, "Bob", "fr")
    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    eng.add_entity(team=0, fid=1, name="Alice", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=1, team_id=1, weapons=[WEAPON_ID],
                   chips=[CHIP_ID], cell=72)
    eng.add_entity(team=1, fid=2, name="Bob", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=2, team_id=2, weapons=[WEAPON_ID],
                   chips=[CHIP_ID], cell=144)
    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144])

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        target = None; bd = 10 ** 9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue
            d = eng.cell_distance2(my_cell, eng.entity_cell(i))
            if d < bd:
                bd = d; target = i
        if target is None:
            return 0
        target_cell = eng.entity_cell(target)
        eng.move_toward(idx, target_cell, max_mp=6)
        if eng.entity_alive(target):
            target_cell = eng.entity_cell(target)
            eng.use_chip(idx, CHIP_ID, target_cell)
        for _ in range(8):
            if not eng.entity_alive(target): break
            target_cell = eng.entity_cell(target)
            rc = eng.fire_weapon(idx, WEAPON_ID, target_cell)
            if rc <= 0: break
        return 0

    eng.set_ai_callback(my_ai)
    eng.run()
    return eng


def main():
    p = argparse.ArgumentParser()
    p.add_argument(
        "--out",
        default="C:/Users/aurel/Desktop/Training Weights Leekwars/"
                "Leekwars-Tools/leek-wars/public/static/report.json",
        help="path to write report.json (default: leek-wars-client public/static/)",
    )
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--turns", type=int, default=20)
    args = p.parse_args()

    eng = run_demo_fight(seed=args.seed, max_turns=args.turns)

    leeks = [
        build_leek(id=0, team=1, name="Alice", cell=72,
                   weapons=[WEAPON_ID], chips=[CHIP_ID], farmer=1),
        build_leek(id=1, team=2, name="Bob", cell=144,
                   weapons=[WEAPON_ID], chips=[CHIP_ID], farmer=2),
    ]
    report = build_report(eng, leeks, team1_ids=[0], team2_ids=[1])

    write_report(args.out, report)

    n_actions = len(report["fight"]["actions"])
    winner = report.get("winner", -1)
    print(f"wrote {args.out}")
    print(f"  fight: {n_actions} actions, winner = {winner}")
    print(f"  open  http://localhost:5173/fight/local  in your browser")


if __name__ == "__main__":
    main()
