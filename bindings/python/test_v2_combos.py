"""V2 combos parity: weapon + damage chip + summon chip simultaneous,
across multiple seeds and loadouts. Tests that complex action stream
interactions stay byte-identical between v2 and Python upstream.
"""
from __future__ import annotations
import argparse, json, os, sys, random
import leekwars_c._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path: sys.path.insert(0, PY_DIR)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_v2_summon as base


def run_v2_combo(seed, weapon_id, dmg_chip_id, summon_chip_id):
    eng = _v2.Engine()
    raw = base.WEAPONS[weapon_id]
    eng.add_weapon(item_id=weapon_id, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]],
                    passive_effects=[(int(e["id"]), float(e["value1"]),
                                       float(e["value2"]), int(e["turns"]),
                                       int(e["targets"]), int(e["modifiers"]))
                                      for e in raw.get("passive_effects", [])])
    base.register_all_chips_v2(eng)
    base.register_summon_templates_v2(eng)
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_team(1, "T1"); eng.add_team(2, "T2")
    eng.add_entity(team=0, fid=1, name="A", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=1, team_id=1, weapons=[weapon_id],
                   chips=[dmg_chip_id, summon_chip_id], cell=72)
    eng.add_entity(team=1, fid=2, name="B", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=2, team_id=2, weapons=[weapon_id],
                   chips=[dmg_chip_id, summon_chip_id], cell=144)
    eng.set_seed(seed); eng.set_max_turns(20)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144])

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        target = None; best_dist = 10**9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue
            d = eng.cell_distance2(my_cell, eng.entity_cell(i))
            if d < best_dist: best_dist = d; target = i
        if target is None: return 0
        target_cell = eng.entity_cell(target)
        ops = 0
        # Turn 1 entity 0: summon
        if turn == 1 and idx == 0:
            for delta in (1, -1, 18, -18):
                summon_cell = my_cell + delta
                if 0 <= summon_cell < 700:
                    rc = eng.use_chip(idx, summon_chip_id, summon_cell)
                    if rc > 0: ops += 1; break
        # Damage chip on enemy (range > 0)
        rc = eng.use_chip(idx, dmg_chip_id, target_cell)
        if rc > 0: ops += 1
        # Fire weapon
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


def run_py_combo(seed, weapon_id, dmg_chip_id, summon_chip_id):
    from leekwars.generator import Generator
    from leekwars.statistics.statistics_manager import DefaultStatisticsManager
    from leekwars.scenario.scenario import Scenario
    from leekwars.scenario.farmer_info import FarmerInfo
    from leekwars.scenario.team_info import TeamInfo
    from leekwars.scenario.entity_info import EntityInfo
    from leekwars.state.state import State as PyState
    from leekwars.weapons import weapons as PyWeapons
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
            enemy_id = fight_class.getNearestEnemy(ai)
            if enemy_id < 0: return
            enemy = ai.getFight().getEntity(enemy_id)
            turn = ai.getFight().getState().getOrder().getTurn()
            if turn == 1 and me.getFId() == 0:
                for delta in (1, -1, 18, -18):
                    try:
                        chip_class.useChipOnCell(ai, summon_chip_id, me.getCell().getId() + delta)
                        break
                    except Exception:
                        pass
            try:
                chip_class.useChipOnCell(ai, dmg_chip_id, enemy.getCell().getId())
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
    sc.addEntity(0, mk(1, 1, "A", [dmg_chip_id, summon_chip_id]))
    sc.addEntity(1, mk(2, 2, "B", [dmg_chip_id, summon_chip_id]))
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

    weapons = [37, 38, 42, 47]  # pistol/machine_gun/laser/m_laser
    dmg_chips = [5, 7, 31, 33, 36, 416]  # flame/rock/iceberg/lightning/meteorite/thunder
    summon_chips = [73, 74, 75, 76, 77, 78, 79, 142, 166, 167]

    print(f"Combos parity: {len(weapons)} weapons x {len(dmg_chips)} dmg chips x {len(summon_chips)} summons "
          f"x {args.seeds} seeds = {len(weapons)*len(dmg_chips)*len(summon_chips)*args.seeds} trials\n")

    n_pass = 0; n_total = 0; failures = []
    rng = random.Random(0)
    combos = [(w, d, s) for w in weapons for d in dmg_chips for s in summon_chips]
    rng.shuffle(combos)
    for (w, d, s) in combos:
        n_ok = 0
        for sd in range(1, args.seeds + 1):
            try:
                v_stream, vw = run_v2_combo(sd, w, d, s)
                p_stream, pw = run_py_combo(sd, w, d, s)
            except Exception:
                continue
            vn = base.normalize(v_stream, "v2"); pn = base.normalize(p_stream, "py")
            f, an, bn, av, pv = diff(vn, pn)
            if f == -1:
                n_ok += 1
            else:
                if len(failures) < 3:
                    failures.append((w, d, s, sd, f, av, pv))
        n_total += args.seeds
        n_pass += n_ok
        if n_ok != args.seeds:
            print(f"  W={w} D={d} S={s}: {n_ok}/{args.seeds}")

    print(f"\nTOTAL: {n_pass}/{n_total} byte-identical ({100*n_pass//max(1,n_total)}%)")
    for w, d, s, sd, f, av, pv in failures:
        print(f"  W={w} D={d} S={s} seed={sd} @ idx {f}")
        print(f"    v2: {av}")
        print(f"    py: {pv}")


if __name__ == "__main__":
    main()
