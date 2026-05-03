"""V2 mass chip sweep across ALL 109 chips. AI picks the right target
based on chip range (self if min_range=0, enemy cell otherwise).
"""
from __future__ import annotations
import argparse, json, os, sys
import leekwars_c_v2._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path: sys.path.insert(0, PY_DIR)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_v2_chip as base


def run_v2_smart(seed, weapon_id, chip_id, a_cell, b_cell):
    """Same as base.run_v2 but the AI picks an appropriate target cell:
       - if chip min_range=0 -> cast on self
       - else -> cast on the enemy's cell
    """
    eng = _v2.Engine()
    raw = base.WEAPONS[weapon_id]
    eng.add_weapon(item_id=weapon_id, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]])
    chip = base.CHIPS[chip_id]
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

    chip_min_range = int(chip["min_range"])

    def my_ai(idx, turn):
        ops = 0
        my_cell = eng.entity_cell(idx)
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        target = None
        for i in range(n):
            if i != idx and eng.entity_alive(i) and eng.entity_team(i) != my_team:
                target = i; break
        target_cell = eng.entity_cell(target) if target is not None else my_cell
        chip_cell = my_cell if chip_min_range == 0 else target_cell
        rc = eng.use_chip(idx, chip_id, chip_cell)
        if rc > 0: ops += 1
        if target is None: return ops
        # Mirror py weapon_class.useWeapon: refuse to fire at dead target.
        if not eng.entity_alive(target): return ops
        for _ in range(8):
            if not eng.entity_alive(target): break
            rc = eng.fire_weapon(idx, weapon_id, target_cell)
            if rc <= 0: break
            ops += 1
        return ops

    eng.set_ai_callback(my_ai)
    eng.run()
    return eng.stream_dump(), eng.winner


def run_py_smart(seed, weapon_id, chip_id, a_cell, b_cell):
    from leekwars.generator import Generator
    from leekwars.statistics.statistics_manager import DefaultStatisticsManager
    from leekwars.scenario.scenario import Scenario
    from leekwars.scenario.farmer_info import FarmerInfo
    from leekwars.scenario.team_info import TeamInfo
    from leekwars.scenario.entity_info import EntityInfo
    from leekwars.state.state import State as PyState
    from leekwars.weapons import weapons as PyWeapons
    from leekwars.classes import weapon_class, chip_class, fight_class

    chip_min_range = int(base.CHIPS[chip_id]["min_range"])

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
            if enemy_id < 0:
                target_cell_id = me.getCell().getId()
            else:
                enemy = ai.getFight().getEntity(enemy_id)
                target_cell_id = enemy.getCell().getId()
            chip_cell_id = me.getCell().getId() if chip_min_range == 0 else target_cell_id
            try:
                chip_class.useChipOnCell(ai, chip_id, chip_cell_id)
            except Exception:
                pass
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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=5)
    args = p.parse_args()

    chips_by_id = base.CHIPS
    candidates = sorted(chips_by_id.keys())

    print(f"Mass chip sweep: ALL {len(candidates)} chips x {args.seeds} seeds\n")
    n_pass = 0; n_total = 0; failures = []
    for cid in candidates:
        chip = chips_by_id[cid]
        n_ok = 0
        for s in range(1, args.seeds + 1):
            try:
                v_stream, vw = run_v2_smart(s, 37, cid, 72, 144)
                p_stream, pw = run_py_smart(s, 37, cid, 72, 144)
            except Exception as ex:
                continue
            vn = base.normalize(v_stream, "v2"); pn = base.normalize(p_stream, "py")
            f, an, bn, av, pv = base.diff(vn, pn)
            if f == -1:
                n_ok += 1
            else:
                if len(failures) < 5:
                    failures.append((cid, chip["name"], s, f, av, pv))
        n_total += args.seeds
        n_pass += n_ok
        status = "OK" if n_ok == args.seeds else f"{n_ok}/{args.seeds}"
        rng = chip["min_range"]
        print(f"  chip {cid:>4} ({chip['name']:<28}) rng={rng}: {status}")

    print(f"\nTOTAL: {n_pass}/{n_total} byte-identical ({100*n_pass//max(1,n_total)}%)")
    for cid, name, seed, idx, av, pv in failures:
        print(f"  chip {cid} ({name}) seed {seed} @ idx {idx}")
        print(f"    v2: {av}")
        print(f"    py: {pv}")


if __name__ == "__main__":
    main()
