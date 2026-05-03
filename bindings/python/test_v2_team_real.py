"""V2 multi-entity TEAM parity with REAL AI (move + chip + weapon).

Each entity:
  1. picks the nearest live enemy
  2. moves toward it if out of weapon range (uses MP)
  3. casts a damage chip if available, in range, and TP allows
  4. fires the weapon while in range and TP allows

Mirrors the basic.leek pattern (getNearestEnemy + moveToward + useChip + useWeapon)
but in pure Python on both sides (v2 callback + py upstream class).

Usage:
    python test_v2_team_real.py --per-team 4 --seeds 10
    python test_v2_team_real.py --per-team 6 --seeds 5
"""
from __future__ import annotations
import argparse, json, os, sys
import leekwars_c._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path: sys.path.insert(0, PY_DIR)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_v2_summon as base  # for normalize, WEAPONS, CHIPS, register helpers

# ---- Loadout ----
WEAPON_ID = 37   # pistol (range 1-7, line)
CHIP_ID   = 5    # flame (damage chip, range 0-7, los)
WITH_MOVE = True  # toggle to disable moveToward (isolate move pathfinding bugs)


def cells_for_team(team_id: int, n: int):
    """Return n distinct cells per team, on opposite sides of an 18x18 map.
    Use the same diamond formula as test_v2_multi.py but with a wider
    vertical spread so up to 6 entities fit.
    """
    if team_id == 0:
        base_xy = [(3, 0), (3, 2), (3, -2), (3, 4), (4, -3), (4, 3)]
    else:
        base_xy = [(9, 0), (9, 2), (9, -2), (9, 4), (8, -3), (8, 3)]
    return [x * 18 + 17 * y for (x, y) in base_xy[:n]]


def run_v2(seed: int, per_team: int, max_turns: int = 25) -> tuple:
    eng = _v2.Engine()
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
    base.register_all_chips_v2(eng)
    cells_a = cells_for_team(0, per_team)
    cells_b = cells_for_team(1, per_team)
    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    fid = 1
    for i in range(per_team):
        eng.add_farmer(fid, f"A{i+1}", "fr")
        eng.add_entity(team=0, fid=fid, name=f"A{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=0,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=fid, team_id=1, weapons=[WEAPON_ID],
                       chips=[CHIP_ID], cell=cells_a[i])
        fid += 1
    for i in range(per_team):
        eng.add_farmer(fid, f"B{i+1}", "fr")
        eng.add_entity(team=1, fid=fid, name=f"B{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=0,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=fid, team_id=2, weapons=[WEAPON_ID],
                       chips=[CHIP_ID], cell=cells_b[i])
        fid += 1
    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_type(2)  # TYPE_TEAM
    eng.set_custom_map(obstacles={}, team1=cells_a, team2=cells_b)

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        target = None; bd = 10**9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue
            d = eng.cell_distance2(my_cell, eng.entity_cell(i))  # squared euclidean
            if d < bd: bd = d; target = i
        if target is None: return 0
        target_cell = eng.entity_cell(target)
        if WITH_MOVE:
            eng.move_toward(idx, target_cell, max_mp=6)
        # Cast damage chip on enemy
        if eng.entity_alive(target):
            target_cell = eng.entity_cell(target)
            eng.use_chip(idx, CHIP_ID, target_cell)
        # Fire weapon while alive + in range
        for _ in range(8):
            if not eng.entity_alive(target): break
            target_cell = eng.entity_cell(target)
            rc = eng.fire_weapon(idx, WEAPON_ID, target_cell)
            if rc <= 0: break
        return 0

    eng.set_ai_callback(my_ai)
    eng.run()
    return eng.stream_dump(), eng.winner


def run_py(seed: int, per_team: int, max_turns: int = 25) -> tuple:
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
                w = PyWeapons.getWeapon(WEAPON_ID)
                if w is not None: me.setWeapon(w)
            enemy_id = fight_class.getNearestEnemy(ai)
            if enemy_id < 0: return
            enemy = ai.getFight().getEntity(enemy_id)
            # Move toward enemy
            if WITH_MOVE:
                try:
                    fight_class.moveTowardCell(ai, enemy.getCell().getId(), 6)
                except Exception:
                    pass
            if enemy.isDead(): return
            # Cast damage chip
            try:
                chip_class.useChipOnCell(ai, CHIP_ID, enemy.getCell().getId())
            except Exception: pass
            # Fire weapon
            for _ in range(8):
                w = me.getWeapon()
                if w is None or me.getTP() < w.getCost(): break
                if not ai.getState().getMap().canUseAttack(me.getCell(), enemy.getCell(), w.getAttack()): break
                try: r = weapon_class.useWeapon(ai, enemy_id)
                except Exception: break
                if r <= 0 or enemy.isDead() or me.isDead(): break

    sc = Scenario()
    sc.seed = seed; sc.maxTurns = max_turns
    sc.type = PyState.TYPE_TEAM
    sc.context = PyState.CONTEXT_TEST
    cells_a = cells_for_team(0, per_team)
    cells_b = cells_for_team(1, per_team)
    sc.map = {"id": 0, "obstacles": {}, "team1": cells_a, "team2": cells_b}
    t1 = TeamInfo(); t1.id = 1; t1.name = "TA"
    t2 = TeamInfo(); t2.id = 2; t2.name = "TB"
    sc.teams[1] = t1; sc.teams[2] = t2

    fid = 1
    for i in range(per_team):
        f = FarmerInfo(); f.id = fid; f.name = f"A{i+1}"; f.country = "fr"
        sc.farmers[fid] = f
        e = EntityInfo()
        e.id = fid; e.name = f"A{i+1}"; e.type = 0
        e.farmer = fid; e.team = 1
        e.level = 100; e.life = 2500
        e.strength = 200; e.agility = 0
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100; e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [WEAPON_ID]; e.chips = [CHIP_ID]
        e.ai_function = ScriptedAgent()
        sc.addEntity(0, e)
        fid += 1
    for i in range(per_team):
        f = FarmerInfo(); f.id = fid; f.name = f"B{i+1}"; f.country = "fr"
        sc.farmers[fid] = f
        e = EntityInfo()
        e.id = fid; e.name = f"B{i+1}"; e.type = 0
        e.farmer = fid; e.team = 2
        e.level = 100; e.life = 2500
        e.strength = 200; e.agility = 0
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100; e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [WEAPON_ID]; e.chips = [CHIP_ID]
        e.ai_function = ScriptedAgent()
        sc.addEntity(1, e)
        fid += 1

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
    p.add_argument("--per-team", type=int, default=4)
    p.add_argument("--seeds", type=int, default=10)
    p.add_argument("--no-move", action="store_true", help="disable moveToward")
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()
    global WITH_MOVE
    WITH_MOVE = not args.no_move

    print(f"V2 team REAL-AI parity ({args.per_team}v{args.per_team}, {args.seeds} seeds, move={WITH_MOVE})")
    print(f"  weapon={WEAPON_ID} (pistol)  chip={CHIP_ID} (flame)\n")

    n_ok = 0; failures = []
    for sd in range(1, args.seeds + 1):
        try:
            v_stream, vw = run_v2(sd, args.per_team)
            p_stream, pw = run_py(sd, args.per_team)
        except Exception as ex:
            print(f"  seed {sd}: ERR {type(ex).__name__}: {ex}")
            if args.verbose:
                import traceback; traceback.print_exc()
            continue
        vn = base.normalize(v_stream, "v2"); pn = base.normalize(p_stream, "py")
        f, an, bn, av, pv = diff(vn, pn)
        if f == -1:
            n_ok += 1
            if args.verbose:
                print(f"  seed {sd:>3}: OK n={an} w v2={vw} py={pw}")
        else:
            failures.append((sd, f, an, bn, av, pv, vw, pw))
            if args.verbose:
                print(f"  seed {sd:>3}: DIVERGE @ {f} v2_n={an} py_n={bn}")

    print(f"\nRESULTS: {n_ok}/{args.seeds} byte-identical")
    for sd, f, an, bn, av, pv, vw, pw in failures[:3]:
        print(f"  seed {sd}: idx {f}")
        print(f"    v2 ({an}, w={vw}): {av}")
        print(f"    py ({bn}, w={pw}): {pv}")


if __name__ == "__main__":
    main()
