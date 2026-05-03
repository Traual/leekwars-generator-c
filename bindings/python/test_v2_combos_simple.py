"""V2 simpler combos: weapon + 2 chips (no summons), random selection.
Verifies multi-chip + weapon parity in non-summon scenarios."""
from __future__ import annotations
import argparse, json, os, sys, random
import leekwars_c._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path: sys.path.insert(0, PY_DIR)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_v2_chip as base


def run_v2(seed, weapon_id, chip_a, chip_b):
    eng = _v2.Engine()
    raw = base.WEAPONS[weapon_id]
    eng.add_weapon(item_id=weapon_id, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]],
                    passive_effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                                       int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                                      for e in raw.get("passive_effects", [])])
    for cid in (chip_a, chip_b):
        c = base.CHIPS[cid]
        eng.add_chip(item_id=cid, name=c["name"], cost=int(c["cost"]),
                      min_range=int(c["min_range"]), max_range=int(c["max_range"]),
                      launch_type=int(c["launch_type"]), area_id=int(c.get("area", 1)),
                      los=bool(c.get("los", True)), max_uses=int(c.get("max_uses", -1)),
                      cooldown=int(c.get("cooldown", 0)),
                      initial_cooldown=int(c.get("initial_cooldown", 0)),
                      team_cooldown=bool(c.get("team_cooldown", False)),
                      level=int(c.get("level", 1)),
                      chip_type=int(c.get("type", 0)),
                      template_id=int(c.get("template", 0)),
                      effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                                int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                               for e in c["effects"]])
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_team(1, "T1"); eng.add_team(2, "T2")
    eng.add_entity(team=0, fid=1, name="A", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=1, team_id=1, weapons=[weapon_id],
                   chips=[chip_a, chip_b], cell=72)
    eng.add_entity(team=1, fid=2, name="B", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=2, team_id=2, weapons=[weapon_id],
                   chips=[chip_a, chip_b], cell=144)
    eng.set_seed(seed); eng.set_max_turns(20)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144])

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        target = None; bd = 10**9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue
            d = eng.cell_distance2(my_cell, eng.entity_cell(i))
            if d < bd: bd = d; target = i
        if target is None: return 0
        target_cell = eng.entity_cell(target)
        ops = 0
        for cid in (chip_a, chip_b):
            c = base.CHIPS[cid]
            chip_cell = my_cell if c["min_range"] == 0 else target_cell
            rc = eng.use_chip(idx, cid, chip_cell)
            if rc > 0: ops += 1
        if not eng.entity_alive(target): return ops
        for _ in range(8):
            if not eng.entity_alive(target): break
            target_cell = eng.entity_cell(target)
            rc = eng.fire_weapon(idx, weapon_id, target_cell)
            if rc <= 0: break
            ops += 1
        return ops

    eng.set_ai_callback(my_ai)
    eng.run()
    return eng.stream_dump(), eng.winner


def run_py(seed, weapon_id, chip_a, chip_b):
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

    chip_ranges = {chip_a: int(base.CHIPS[chip_a]["min_range"]),
                   chip_b: int(base.CHIPS[chip_b]["min_range"])}

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
            for cid in (chip_a, chip_b):
                rng = chip_ranges[cid]
                cell_id = me.getCell().getId() if rng == 0 else enemy.getCell().getId()
                try: chip_class.useChipOnCell(ai, cid, cell_id)
                except Exception: pass
            for _ in range(8):
                w = me.getWeapon()
                if w is None or me.getTP() < w.getCost(): break
                if not ai.getState().getMap().canUseAttack(me.getCell(), enemy.getCell(), w.getAttack()): break
                try:
                    r = weapon_class.useWeapon(ai, enemy_id)
                except Exception: break
                if r <= 0 or enemy.isDead() or me.isDead(): break

    sc = Scenario()
    sc.seed = seed; sc.maxTurns = 20
    sc.type = PyState.TYPE_SOLO
    sc.context = PyState.CONTEXT_TEST
    sc.map = {"id": 0, "obstacles": {}, "team1": [72], "team2": [144]}
    f1 = FarmerInfo(); f1.id = 1; f1.name = "A"; f1.country = "fr"
    f2 = FarmerInfo(); f2.id = 2; f2.name = "B"; f2.country = "fr"
    sc.farmers[1] = f1; sc.farmers[2] = f2
    t1 = TeamInfo(); t1.id = 1; t1.name = "T1"
    t2 = TeamInfo(); t2.id = 2; t2.name = "T2"
    sc.teams[1] = t1; sc.teams[2] = t2

    def mk(eid, fid, name, chips):
        e = EntityInfo()
        e.id = eid; e.name = name; e.type = 0
        e.farmer = fid; e.team = fid
        e.level = 100; e.life = 2500
        e.strength = 200; e.agility = 0
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100; e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [weapon_id]; e.chips = chips
        e.ai_function = ScriptedAgent()
        return e
    sc.addEntity(0, mk(1, 1, "A", [chip_a, chip_b]))
    sc.addEntity(1, mk(2, 2, "B", [chip_a, chip_b]))
    g = Generator()
    out = g.runScenario(sc, None, _NoReg(), _NoStats())
    actions = []
    for a in out.fight.actions:
        try: actions.append(a.getJSON())
        except: pass
    return actions, out.winner


def diff(a, b):
    n = min(len(a), len(b)); f = -1
    for i in range(n):
        if a[i] != b[i]: f = i; break
    if f == -1 and len(a) != len(b): f = n
    return f, len(a), len(b), (a[f] if 0 <= f < len(a) else None), (b[f] if 0 <= f < len(b) else None)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=10)
    args = p.parse_args()

    weapons = [37, 38, 42, 47]
    # All non-summon chips with reasonable cost
    chips = [1, 3, 4, 5, 6, 7, 8, 9, 11, 16, 18, 19, 20, 22, 25, 27, 33, 35, 100, 110, 156]

    rng = random.Random(42)
    combos = []
    for w in weapons:
        for ca in chips:
            for cb in chips:
                if ca < cb:
                    combos.append((w, ca, cb))
    rng.shuffle(combos)
    combos = combos[:50]  # limit for time

    print(f"Combos parity: {len(combos)} weapon+chip+chip combos x {args.seeds} seeds = {len(combos)*args.seeds} trials\n")

    n_pass = 0; n_total = 0; failures = []
    for (w, ca, cb) in combos:
        n_ok = 0
        for sd in range(1, args.seeds + 1):
            try:
                v_stream, vw = run_v2(sd, w, ca, cb)
                p_stream, pw = run_py(sd, w, ca, cb)
            except Exception:
                continue
            vn = base.normalize(v_stream, "v2"); pn = base.normalize(p_stream, "py")
            f, an, bn, av, pv = diff(vn, pn)
            if f == -1: n_ok += 1
            else:
                if len(failures) < 3:
                    failures.append((w, ca, cb, sd, f, av, pv))
        n_total += args.seeds
        n_pass += n_ok
        if n_ok != args.seeds:
            print(f"  W={w} C={ca}+{cb}: {n_ok}/{args.seeds}")

    print(f"\nTOTAL: {n_pass}/{n_total} byte-identical ({100*n_pass//max(1,n_total)}%)")
    for w, ca, cb, sd, f, av, pv in failures:
        print(f"  W={w} C={ca}+{cb} seed={sd} @ idx {f}")
        print(f"    v2: {av}")
        print(f"    py: {pv}")


if __name__ == "__main__":
    main()
