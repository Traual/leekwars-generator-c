"""V2 chip parity test: AI uses pistol AND a chip (bandage = heal) each
turn. Verifies USE_CHIP, HEAL, ADD_CHIP_EFFECT events match Java/Python.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter

import leekwars_c._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path:
    sys.path.insert(0, PY_DIR)

PY_ACT = {
    "START_FIGHT": 0, "END_FIGHT": 4, "PLAYER_DEAD": 5, "NEW_TURN": 6,
    "LEEK_TURN": 7, "END_TURN": 8, "MOVE_TO": 10, "KILL": 11,
    "USE_CHIP": 12, "SET_WEAPON": 13, "STACK_EFFECT": 14, "USE_WEAPON": 16,
    "LOST_LIFE": 101, "HEAL": 103, "VITALITY": 104, "NOVA_DAMAGE": 107,
    "POISON_DAMAGE": 110, "AFTEREFFECT": 111,
    "ADD_WEAPON_EFFECT": 301, "ADD_CHIP_EFFECT": 302,
    "REMOVE_EFFECT": 303, "UPDATE_EFFECT": 304, "REDUCE_EFFECTS": 306,
    "ERROR": 1000, "MAP": 1001, "AI_ERROR": 1002,
}
ID_TO_NAME = {v: k for k, v in PY_ACT.items()}


def normalize(stream, source):
    out = []
    for e in stream:
        if source == "v2":
            t, args = e["type"], e["args"]
        else:
            if not isinstance(e, list) or not e: continue
            t, args = e[0], list(e[1:])
        name = ID_TO_NAME.get(t)
        if name is None or name in ("MAP", "SAY", "SHOW_CELL", "LAMA", "AI_ERROR", "ERROR"): continue
        out.append((name, tuple(args)))
    return out


def load_data():
    with open(os.path.join(PY_DIR, "data", "weapons.json")) as f:
        weap = {int(s["item"]): s for s in json.load(f).values()}
    with open(os.path.join(PY_DIR, "data", "chips.json")) as f:
        chips = {int(s["id"]): s for s in json.load(f).values()}
    return weap, chips


WEAPONS, CHIPS = load_data()


def run_v2(seed, weapon_id, chip_id, a_cell, b_cell):
    eng = _v2.Engine()
    raw = WEAPONS[weapon_id]
    eng.add_weapon(item_id=weapon_id, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]])
    chip = CHIPS[chip_id]
    eng.add_chip(item_id=chip_id, name=chip["name"], cost=int(chip["cost"]),
                  min_range=int(chip["min_range"]), max_range=int(chip["max_range"]),
                  launch_type=int(chip["launch_type"]), area_id=int(chip.get("area", 1)),
                  los=bool(chip.get("los", True)), max_uses=int(chip.get("max_uses", -1)),
                  cooldown=int(chip.get("cooldown", 0)),
                  initial_cooldown=int(chip.get("initial_cooldown", 0)),
                  team_cooldown=bool(chip.get("team_cooldown", False)),
                  level=int(chip.get("level", 1)),
                  chip_type=int(chip.get("type", 0)),
                  template_id=int(chip.get("template", 0)),
                  effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                            int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                           for e in chip["effects"]])
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_team(1, "T1"); eng.add_team(2, "T2")
    eng.add_entity(team=0, fid=1, name="A", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=1, team_id=1, weapons=[weapon_id], chips=[chip_id], cell=a_cell)
    eng.add_entity(team=1, fid=2, name="B", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=2, team_id=2, weapons=[weapon_id], chips=[chip_id], cell=b_cell)
    eng.set_seed(seed); eng.set_max_turns(30)
    eng.set_custom_map(obstacles={}, team1=[a_cell], team2=[b_cell])

    def my_ai(idx, turn):
        # First: try chip on self (heal). Then fire weapon at enemy.
        ops = 0
        my_cell = eng.entity_cell(idx)
        rc = eng.use_chip(idx, chip_id, my_cell)
        if rc > 0: ops += 1
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        target = None
        for i in range(n):
            if i != idx and eng.entity_alive(i) and eng.entity_team(i) != my_team:
                target = i; break
        if target is None: return ops
        target_cell = eng.entity_cell(target)
        for _ in range(8):
            rc = eng.fire_weapon(idx, weapon_id, target_cell)
            if rc <= 0: break
            ops += 1
            if not eng.entity_alive(target): break
        return ops

    eng.set_ai_callback(my_ai)
    eng.run()
    return eng.stream_dump(), eng.winner


def run_py(seed, weapon_id, chip_id, a_cell, b_cell):
    from leekwars.generator import Generator
    from leekwars.statistics.statistics_manager import DefaultStatisticsManager
    from leekwars.scenario.scenario import Scenario
    from leekwars.scenario.farmer_info import FarmerInfo
    from leekwars.scenario.team_info import TeamInfo
    from leekwars.scenario.entity_info import EntityInfo
    from leekwars.state.state import State as PyState
    from leekwars.weapons import weapons as PyWeapons
    from leekwars.chips import chips as PyChips
    from leekwars.classes import weapon_class, chip_class, fight_class

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
            # First: chip on self cell
            try:
                chip_class.useChipOnCell(ai, chip_id, me.getCell().getId())
            except Exception:
                pass
            # Then: pistol on nearest enemy
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
    sc.seed = seed; sc.maxTurns = 30
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
        e.frequency = 100; e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [weapon_id]; e.chips = [chip_id]
        e.ai_function = ScriptedAgent()
        return e
    sc.addEntity(0, mk(1, 1, "A"))
    sc.addEntity(1, mk(2, 2, "B"))

    g = Generator()
    out = g.runScenario(sc, None, _NoReg(), _NoStats())
    actions = []
    for a in out.fight.actions:
        try: actions.append(a.getJSON())
        except: pass
    return actions, out.winner


def diff(a, b):
    n = min(len(a), len(b))
    f = -1
    for i in range(n):
        if a[i] != b[i]:
            f = i; break
    if f == -1 and len(a) != len(b): f = n
    return f, len(a), len(b), (a[f] if 0 <= f < len(a) else None), (b[f] if 0 <= f < len(b) else None)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=20)
    p.add_argument("--weapon", type=int, default=37)
    p.add_argument("--chip", type=int, default=3, help="chip id (3=bandage heal)")
    p.add_argument("--a-cell", type=int, default=72)
    p.add_argument("--b-cell", type=int, default=144)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    print(f"V2 chip parity: weapon {args.weapon}, chip {args.chip} "
          f"({CHIPS[args.chip]['name']}), {args.seeds} seeds\n")

    n_ok = 0; failures = []
    for s in range(1, args.seeds + 1):
        try:
            v_stream, vw = run_v2(s, args.weapon, args.chip, args.a_cell, args.b_cell)
            p_stream, pw = run_py(s, args.weapon, args.chip, args.a_cell, args.b_cell)
        except Exception as ex:
            print(f"  seed {s}: ERR {type(ex).__name__}: {ex}")
            if args.verbose:
                import traceback; traceback.print_exc()
            continue
        vn = normalize(v_stream, "v2"); pn = normalize(p_stream, "py")
        f, an, bn, av, pv = diff(vn, pn)
        if f == -1:
            n_ok += 1
            if args.verbose:
                print(f"  seed {s:>3}: OK n={an} winner v2={vw} py={pw}")
        else:
            if len(failures) < 3:
                failures.append((s, f, an, bn, av, pv, vw, pw))
            if args.verbose:
                print(f"  seed {s:>3}: DIVERGE @ {f} v2_n={an} py_n={bn}")

    print(f"\nRESULTS: {n_ok}/{args.seeds} identical")
    for s, f, an, bn, av, pv, vw, pw in failures:
        print(f"  seed {s}: idx {f}")
        print(f"    v2 ({an}, w={vw}): {av}")
        print(f"    py ({bn}, w={pw}): {pv}")


if __name__ == "__main__":
    main()
