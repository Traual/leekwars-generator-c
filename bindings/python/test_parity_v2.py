"""STRICT byte-by-byte parity test for the v2 (line-by-line port) C engine
against Python upstream.

Key difference from test_action_stream_strict.py:
- Uses the high-level Generator.runScenario API on BOTH sides
- No monkey-patching of Python's RNG / start order / Map.generateMap
- The C engine runs its own scenario init (incl. obstacle generation
  via Map.generateMap), so RNG draws should align naturally

Once v2 is compiled, this should pass 100% on every weapon and every
seed without any fixup code on the Python side.
"""
from __future__ import annotations

import argparse
import json
import os
import sys


def load_weapons_json():
    py_root = "C:/Users/aurel/Desktop/leekwars_generator_python"
    with open(os.path.join(py_root, "data", "weapons.json")) as f:
        return json.load(f)


# ====================================================================
# C side (v2 engine via leekwars_c_v2)
# ====================================================================

def run_c_v2(seed: int, weapon_id: int, a_cell: int, b_cell: int,
             max_turns: int = 30) -> tuple[list, int]:
    """Run a 1v1 fight using the v2 generator.  Returns (action_stream, winner)."""
    import leekwars_c_v2 as engine

    sc = engine.Scenario(
        seed=seed,
        max_turns=max_turns,
        type=engine.FIGHT_TYPE_SOLO,
        context=engine.CONTEXT_TEST,
        custom_map={
            "id": 0,
            "obstacles": {},
            "team1": [a_cell],
            "team2": [b_cell],
        },
    )

    def fire_at_nearest(ai):
        me = ai.get_entity()
        enemy = ai.get_nearest_enemy()
        if enemy is None:
            return
        for _ in range(8):
            r = ai.use_weapon_on(enemy)
            if r <= 0 or enemy.is_dead() or me.is_dead():
                break

    for fid, cell in [(0, a_cell), (1, b_cell)]:
        sc.add_entity(team=fid, info=engine.EntityInfo(
            id=fid + 1, name=f"E{fid}", type=engine.ENTITY_TYPE_LEEK,
            farmer=fid + 1, level=100, life=2500,
            tp=14, mp=6, strength=200, agility=0, frequency=100,
            wisdom=0, resistance=0, science=0, magic=0,
            cores=10, ram=10,
            weapons=[weapon_id], chips=[],
            ai=fire_at_nearest,
        ))

    g = engine.Generator()
    outcome = g.run_scenario(sc)
    return outcome.action_stream, outcome.winner


# ====================================================================
# Python upstream side
# ====================================================================

def run_py(seed: int, weapon_id: int, a_cell: int, b_cell: int,
            max_turns: int = 30) -> tuple[list, int]:
    """Run identical 1v1 via Python upstream Generator.runScenario."""
    sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")
    from leekwars.generator import Generator
    from leekwars.scenario.scenario import Scenario
    from leekwars.scenario.entity_info import EntityInfo
    from leekwars.scenario.farmer_info import FarmerInfo
    from leekwars.scenario.team_info import TeamInfo
    from leekwars.state.state import State as PyState
    from leekwars.statistics.statistics_manager import DefaultStatisticsManager
    from leekwars.leek.register_manager import RegisterManager
    from leekwars.classes import weapon_class, fight_class
    from leekwars.weapons import weapons as PyWeapons

    class _NoOpStats(DefaultStatisticsManager):
        def setGeneratorFight(self, fight): pass
    class _NoOpReg(RegisterManager):
        def getRegisters(self, leek): return None
        def saveRegisters(self, leek, registers, is_new): pass

    def fire_at_nearest(ai):
        me = ai.getEntity()
        # Pre-equip
        if me.getWeapon() is None:
            w = PyWeapons.getWeapon(weapon_id)
            if w is None: return
            me.setWeapon(w)
        enemy_id = fight_class.getNearestEnemy(ai)
        if enemy_id < 0: return
        enemy = ai.getFight().getEntity(enemy_id)
        for _ in range(8):
            w = me.getWeapon()
            if w is None or me.getTP() < w.getCost(): break
            if not ai.getState().getMap().canUseAttack(
                    me.getCell(), enemy.getCell(), w.getAttack()):
                break
            try:
                r = weapon_class.useWeapon(ai, enemy_id)
            except Exception:
                break
            if r <= 0 or enemy.isDead() or me.isDead():
                break

    sc = Scenario()
    sc.seed = seed
    sc.maxTurns = max_turns
    sc.type = PyState.TYPE_SOLO
    sc.context = PyState.CONTEXT_TEST
    sc.map = {"id": 0, "obstacles": {}, "team1": [a_cell], "team2": [b_cell]}
    f1 = FarmerInfo(); f1.id = 1; f1.name = "A"; f1.country = "fr"
    f2 = FarmerInfo(); f2.id = 2; f2.name = "B"; f2.country = "fr"
    sc.farmers[1] = f1; sc.farmers[2] = f2
    t1 = TeamInfo(); t1.id = 1; t1.name = "T1"
    t2 = TeamInfo(); t2.id = 2; t2.name = "T2"
    sc.teams[1] = t1; sc.teams[2] = t2

    def mk(eid, fid, name):
        e = EntityInfo()
        e.id = eid; e.name = name; e.type = 0
        e.farmer = fid; e.team = fid
        e.level = 100; e.life = 2500
        e.strength = 200; e.agility = 0
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100
        e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [weapon_id]; e.chips = []
        e.ai_function = fire_at_nearest
        return e
    sc.addEntity(0, mk(1, 1, "A"))
    sc.addEntity(1, mk(2, 2, "B"))

    g = Generator()
    out = g.runScenario(sc, None, _NoOpReg(), _NoOpStats())
    actions = []
    for action in out.fight.actions:
        try:
            actions.append(action.getJSON())
        except Exception:
            pass
    return actions, out.winner


# ====================================================================
# Diff
# ====================================================================

def diff_streams(py: list, c: list) -> dict:
    n = min(len(py), len(c))
    first = -1
    for i in range(n):
        if py[i] != c[i]:
            first = i
            break
    if first == -1 and len(py) != len(c):
        first = n
    return {
        "identical": first == -1,
        "first_div": first,
        "py_n": len(py),
        "c_n": len(c),
        "py_first": py[first] if 0 <= first < len(py) else None,
        "c_first":  c[first]  if 0 <= first < len(c)  else None,
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=10)
    p.add_argument("--weapon", type=int, default=37, help="canonical weapon id (item field)")
    p.add_argument("--a-cell", type=int, default=72)
    p.add_argument("--b-cell", type=int, default=144)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    print("=" * 70)
    print(" Phase A.X — STRICT parity (Python upstream vs C v2)")
    print("=" * 70)
    print(f" weapon_id={args.weapon} a_cell={args.a_cell} b_cell={args.b_cell}")
    print(f" running {args.seeds} seeds...\n")

    n_ok = 0
    failures = []
    for seed in range(1, args.seeds + 1):
        try:
            py_stream, py_winner = run_py(seed, args.weapon, args.a_cell, args.b_cell)
            c_stream,  c_winner  = run_c_v2(seed, args.weapon, args.a_cell, args.b_cell)
        except Exception as e:
            print(f"  seed {seed}: ERROR {type(e).__name__}: {e}")
            if args.verbose:
                import traceback as _tb; _tb.print_exc()
            continue

        d = diff_streams(py_stream, c_stream)
        if d["identical"]:
            n_ok += 1
            if args.verbose:
                print(f"  seed {seed:>3}: OK (n={d['py_n']})  winner py={py_winner} c={c_winner}")
        else:
            failures.append({"seed": seed, **d, "py_winner": py_winner, "c_winner": c_winner})
            if args.verbose:
                print(f"  seed {seed:>3}: DIVERGE @ idx {d['first_div']}")
                print(f"    py: {d['py_first']}")
                print(f"    c:  {d['c_first']}")

    print(f"\n RESULTS: {n_ok}/{args.seeds} identical")
    for f in failures[:5]:
        print(f"   seed {f['seed']} @ idx {f['first_div']}: py_n={f['py_n']} c_n={f['c_n']}")


if __name__ == "__main__":
    main()
