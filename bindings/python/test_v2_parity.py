"""V2 parity test: leekwars_c (ligne-par-ligne port from Java) vs
the Python upstream engine.

Mirrors the v1 test_action_stream_strict.py methodology:
  * Run an identical scripted "always fire" fight on both engines.
  * Normalise both action streams into canonical tuples.
  * Diff entry by entry; report first divergence + per-type counts.

Usage:
    python test_v2_parity.py --seeds 100 [--weapon 37] [--a-cell 72] [--b-cell 144]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter

import leekwars_c._engine as _v2

# Make the Python upstream importable
PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path:
    sys.path.insert(0, PY_DIR)


# ============= Java action constants ===========================
PY_ACT = {
    "START_FIGHT": 0, "END_FIGHT": 4, "PLAYER_DEAD": 5, "NEW_TURN": 6,
    "LEEK_TURN": 7, "END_TURN": 8, "SUMMON": 9, "MOVE_TO": 10,
    "KILL": 11, "USE_CHIP": 12, "SET_WEAPON": 13, "STACK_EFFECT": 14,
    "CHEST_OPENED": 15, "USE_WEAPON": 16,
    "LOST_PT": 100, "LOST_LIFE": 101, "LOST_PM": 102, "HEAL": 103,
    "VITALITY": 104, "RESURRECT": 105, "LOSE_STRENGTH": 106,
    "NOVA_DAMAGE": 107, "DAMAGE_RETURN": 108, "LIFE_DAMAGE": 109,
    "POISON_DAMAGE": 110, "AFTEREFFECT": 111, "NOVA_VITALITY": 112,
    "LAMA": 201, "SAY": 203, "SHOW_CELL": 205,
    "ADD_WEAPON_EFFECT": 301, "ADD_CHIP_EFFECT": 302,
    "REMOVE_EFFECT": 303, "UPDATE_EFFECT": 304,
    "REDUCE_EFFECTS": 306, "REMOVE_POISONS": 307, "REMOVE_SHACKLES": 308,
    "ERROR": 1000, "MAP": 1001, "AI_ERROR": 1002,
}
ID_TO_NAME = {v: k for k, v in PY_ACT.items()}


def normalize(stream, source: str):
    """Convert a stream of {type, args, [extra]} dicts (v2) or [type, ...] lists
    (Python upstream) to canonical tuples for comparison."""
    out = []
    for e in stream:
        if source == "v2":
            t, args = e["type"], e["args"]
        else:
            if not isinstance(e, list) or not e:
                continue
            t, args = e[0], list(e[1:])
        name = ID_TO_NAME.get(t)
        if name is None:
            continue
        # Drop noisy / non-essential events
        if name in ("MAP", "SAY", "SHOW_CELL", "LAMA", "AI_ERROR", "ERROR"):
            continue
        out.append((name, tuple(args)))
    return out


# ============== v2 engine wrapper ===================================
def run_v2(seed: int, weapon_id: int, a_cell: int, b_cell: int,
            agility: int = 0, max_turns: int = 30) -> tuple:
    eng = _v2.Engine()
    # Register pistol (item_id=37 by default)
    raw = WEAPONS_JSON_BY_ITEM[weapon_id]
    eng.add_weapon(item_id=weapon_id, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]),
                    max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]),
                    area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)),
                    max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]),
                              float(e["value2"]), int(e["turns"]),
                              int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]],
                    passive_effects=[(int(e["id"]), float(e["value1"]),
                                       float(e["value2"]), int(e["turns"]),
                                       int(e["targets"]), int(e["modifiers"]))
                                      for e in raw.get("passive_effects", [])])
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_team(1, "T1"); eng.add_team(2, "T2")
    eng.add_entity(team=0, fid=1, name="A", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=agility,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=1, team_id=1, weapons=[weapon_id], cell=a_cell)
    eng.add_entity(team=1, fid=2, name="B", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=agility,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=2, team_id=2, weapons=[weapon_id], cell=b_cell)
    eng.set_seed(seed)
    eng.set_max_turns(max_turns)
    eng.set_custom_map(obstacles={}, team1=[a_cell], team2=[b_cell])

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        target = None
        for i in range(n):
            if i != idx and eng.entity_alive(i) and eng.entity_team(i) != my_team:
                target = i
                break
        if target is None:
            return 0
        target_cell = eng.entity_cell(target)
        fired = 0
        for _ in range(8):
            rc = eng.fire_weapon(idx, weapon_id, target_cell)
            if rc <= 0:
                break
            fired += 1
            if not eng.entity_alive(target):
                break
        return fired

    eng.set_ai_callback(my_ai)
    eng.run()
    return eng.stream_dump(), eng.winner


# ============== Python upstream wrapper =============================
def run_py(seed: int, weapon_id: int, a_cell: int, b_cell: int,
            agility: int = 0, max_turns: int = 30) -> tuple:
    from leekwars.generator import Generator
    from leekwars.statistics.statistics_manager import DefaultStatisticsManager
    from leekwars.scenario.scenario import Scenario
    from leekwars.scenario.farmer_info import FarmerInfo
    from leekwars.scenario.team_info import TeamInfo
    from leekwars.scenario.entity_info import EntityInfo
    from leekwars.state.state import State as PyState
    from leekwars.weapons import weapons as PyWeapons
    from leekwars.classes import weapon_class, fight_class

    class _NoStats(DefaultStatisticsManager):
        def setGeneratorFight(self, fight): pass
    class _NoReg:
        def getRegisters(self, leek): return None
        def saveRegisters(self, leek, reg, is_new): pass

    class ScriptedAgent:
        def __call__(self, ai):
            me = ai.getEntity()
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
                if not ai.getState().getMap().canUseAttack(me.getCell(), enemy.getCell(), w.getAttack()): break
                try:
                    r = weapon_class.useWeapon(ai, enemy_id)
                except Exception:
                    break
                if r <= 0 or enemy.isDead() or me.isDead(): break

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
        e.strength = 200; e.agility = agility
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100
        e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [weapon_id]; e.chips = []
        e.ai_function = ScriptedAgent()
        return e
    sc.addEntity(0, mk(1, 1, "A"))
    sc.addEntity(1, mk(2, 2, "B"))

    # No monkey-patch: both engines run their natural compute_start_order
    # (Java's Bradley-Terry-on-frequencies). With identical frequencies
    # (100 each) and identical seed, the two LCGs should pick the same
    # team order if and only if they consume the same number of draws.
    g = Generator()
    out = g.runScenario(sc, None, _NoReg(), _NoStats())

    actions = []
    for action in out.fight.actions:
        try:
            actions.append(action.getJSON())
        except Exception:
            pass
    return actions, out.winner


# ============== diff =================================================
def diff_streams(a_norm, b_norm):
    """Return (identical, first_div_idx, a_only_types, b_only_types)."""
    a_types = Counter(t[0] for t in a_norm)
    b_types = Counter(t[0] for t in b_norm)
    common = set(a_types) & set(b_types)
    a_only = {k: v for k, v in a_types.items() if k not in common}
    b_only = {k: v for k, v in b_types.items() if k not in common}
    n = min(len(a_norm), len(b_norm))
    first = -1
    for i in range(n):
        if a_norm[i] != b_norm[i]:
            first = i; break
    if first == -1 and len(a_norm) != len(b_norm):
        first = n
    return {
        "identical": first == -1,
        "first_div": first,
        "a_n": len(a_norm), "b_n": len(b_norm),
        "a_types": dict(a_types), "b_types": dict(b_types),
        "a_only_types": a_only, "b_only_types": b_only,
        "a_at_div": a_norm[first] if 0 <= first < len(a_norm) else None,
        "b_at_div": b_norm[first] if 0 <= first < len(b_norm) else None,
    }


# ============== weapons.json =========================================
def load_weapons():
    with open(os.path.join(PY_DIR, "data", "weapons.json")) as f:
        data = json.load(f)
    by_item = {int(spec["item"]): spec for spec in data.values()}
    return by_item


WEAPONS_JSON_BY_ITEM = load_weapons()


# ============== main =================================================
def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=10)
    p.add_argument("--weapon", type=int, default=37, help="item id (37=pistol, 408=odachi)")
    p.add_argument("--a-cell", type=int, default=72)
    p.add_argument("--b-cell", type=int, default=144)
    p.add_argument("--agility", type=int, default=0)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    raw = WEAPONS_JSON_BY_ITEM[args.weapon]
    print("=" * 70)
    print(" V2 parity test (line-by-line Java port vs Python upstream)")
    print("=" * 70)
    print(f" weapon: id={args.weapon} name={raw['name']!r}")
    print(f" cells:  A={args.a_cell} B={args.b_cell} agility={args.agility}")
    print(f" running {args.seeds} seeds...\n")

    n_ok = 0
    failures = []
    for seed in range(1, args.seeds + 1):
        try:
            v2_stream, v2_winner = run_v2(seed, args.weapon, args.a_cell, args.b_cell,
                                            args.agility)
            py_stream, py_winner = run_py(seed, args.weapon, args.a_cell, args.b_cell,
                                            args.agility)
        except Exception as ex:
            print(f"  seed {seed}: ERROR {type(ex).__name__}: {ex}")
            if args.verbose:
                import traceback; traceback.print_exc()
            continue

        v2_norm = normalize(v2_stream, "v2")
        py_norm = normalize(py_stream, "py")
        d = diff_streams(v2_norm, py_norm)
        if d["identical"]:
            n_ok += 1
            if args.verbose:
                print(f"  seed {seed:>3}: OK n={d['a_n']} winner v2={v2_winner} py={py_winner}")
        else:
            if len(failures) < 3:
                failures.append({"seed": seed, **d, "v2_winner": v2_winner, "py_winner": py_winner})
            if args.verbose:
                print(f"  seed {seed:>3}: DIVERGE @ idx {d['first_div']} (v2_n={d['a_n']} py_n={d['b_n']})")
                print(f"    v2: {d['a_at_div']}")
                print(f"    py: {d['b_at_div']}")

    print()
    print(f" RESULTS: {n_ok}/{args.seeds} identical")
    if failures:
        print()
        print(" First failures:")
        for f in failures:
            print(f"  seed {f['seed']}: idx {f['first_div']}")
            print(f"    v2 ({f['a_n']} entries, winner {f['v2_winner']}): {f['a_at_div']}")
            print(f"    py ({f['b_n']} entries, winner {f['py_winner']}): {f['b_at_div']}")
            print(f"    v2_only types: {f['a_only_types']}")
            print(f"    py_only types: {f['b_only_types']}")


if __name__ == "__main__":
    main()
