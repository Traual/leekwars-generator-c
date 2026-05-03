"""V2 random-map parity test: NO custom map -> engine generates random
obstacles. Verifies that the random map gen + obstacle placement +
LoS-blocking + entity placement are byte-identical between v2 and Python
upstream for arbitrary seeds.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter

import leekwars_c_v2._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path:
    sys.path.insert(0, PY_DIR)


PY_ACT = {
    "START_FIGHT": 0, "END_FIGHT": 4, "PLAYER_DEAD": 5, "NEW_TURN": 6,
    "LEEK_TURN": 7, "END_TURN": 8, "SUMMON": 9, "MOVE_TO": 10,
    "KILL": 11, "USE_CHIP": 12, "SET_WEAPON": 13, "STACK_EFFECT": 14,
    "CHEST_OPENED": 15, "USE_WEAPON": 16,
    "LOST_PT": 100, "LOST_LIFE": 101, "LOST_PM": 102, "HEAL": 103,
    "VITALITY": 104, "RESURRECT": 105, "LOSE_STRENGTH": 106,
    "NOVA_DAMAGE": 107, "DAMAGE_RETURN": 108, "LIFE_DAMAGE": 109,
    "POISON_DAMAGE": 110, "AFTEREFFECT": 111, "NOVA_VITALITY": 112,
    "ADD_WEAPON_EFFECT": 301, "ADD_CHIP_EFFECT": 302,
    "REMOVE_EFFECT": 303, "UPDATE_EFFECT": 304,
    "REDUCE_EFFECTS": 306, "REMOVE_POISONS": 307, "REMOVE_SHACKLES": 308,
    "ERROR": 1000, "MAP": 1001, "AI_ERROR": 1002,
}
ID_TO_NAME = {v: k for k, v in PY_ACT.items()}


def normalize(stream, source: str):
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
        if name in ("MAP", "SAY", "SHOW_CELL", "LAMA", "AI_ERROR", "ERROR"):
            continue
        out.append((name, tuple(args)))
    return out


def run_v2(seed: int, weapon_id: int, agility: int = 0,
            max_turns: int = 30) -> tuple:
    eng = _v2.Engine()
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
                             for e in raw["effects"]])
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_team(1, "T1"); eng.add_team(2, "T2")
    eng.add_entity(team=0, fid=1, name="A", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=agility,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=1, team_id=1, weapons=[weapon_id])  # NO cell -> random
    eng.add_entity(team=1, fid=2, name="B", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=agility,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=2, team_id=2, weapons=[weapon_id])  # NO cell -> random
    eng.set_seed(seed)
    eng.set_max_turns(max_turns)
    # NO set_custom_map -> engine generates random obstacles + random cells

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        target = None
        for i in range(n):
            if i != idx and eng.entity_alive(i) and eng.entity_team(i) != my_team:
                target = i; break
        if target is None: return 0
        target_cell = eng.entity_cell(target)
        fired = 0
        for _ in range(8):
            rc = eng.fire_weapon(idx, weapon_id, target_cell)
            if rc <= 0: break
            fired += 1
            if not eng.entity_alive(target): break
        return fired

    eng.set_ai_callback(my_ai)
    eng.run()
    return eng.stream_dump(), eng.winner


def run_py(seed: int, weapon_id: int, agility: int = 0,
            max_turns: int = 30) -> tuple:
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
    # NO sc.map -> engine generates random map

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

    g = Generator()
    out = g.runScenario(sc, None, _NoReg(), _NoStats())

    actions = []
    for action in out.fight.actions:
        try:
            actions.append(action.getJSON())
        except Exception:
            pass
    return actions, out.winner


def diff_streams(a, b):
    n = min(len(a), len(b))
    first = -1
    for i in range(n):
        if a[i] != b[i]:
            first = i; break
    if first == -1 and len(a) != len(b):
        first = n
    return {"identical": first == -1, "first": first,
            "a_n": len(a), "b_n": len(b),
            "a_at": a[first] if 0 <= first < len(a) else None,
            "b_at": b[first] if 0 <= first < len(b) else None}


def load_weapons():
    with open(os.path.join(PY_DIR, "data", "weapons.json")) as f:
        data = json.load(f)
    return {int(spec["item"]): spec for spec in data.values()}


WEAPONS_JSON_BY_ITEM = load_weapons()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=20)
    p.add_argument("--weapon", type=int, default=37)
    p.add_argument("--agility", type=int, default=0)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    print(f"V2 random-map parity, weapon {args.weapon}, agility {args.agility}, "
          f"{args.seeds} seeds (no custom_map -> random obstacles + cells)\n")

    n_ok = 0
    failures = []
    for seed in range(1, args.seeds + 1):
        try:
            v2_stream, v2_w = run_v2(seed, args.weapon, args.agility)
            py_stream, py_w = run_py(seed, args.weapon, args.agility)
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
                print(f"  seed {seed:>3}: OK n={d['a_n']} winner v2={v2_w} py={py_w}")
        else:
            if len(failures) < 3:
                failures.append({"seed": seed, **d, "v2_w": v2_w, "py_w": py_w})
            if args.verbose:
                print(f"  seed {seed:>3}: DIVERGE @ {d['first']} v2_n={d['a_n']} py_n={d['b_n']}")

    print(f"\nRESULTS: {n_ok}/{args.seeds} identical")
    for f in failures:
        print(f"  seed {f['seed']}: idx {f['first']}")
        print(f"    v2 ({f['a_n']}, w={f['v2_w']}): {f['a_at']}")
        print(f"    py ({f['b_n']}, w={f['py_w']}): {f['b_at']}")


if __name__ == "__main__":
    main()
