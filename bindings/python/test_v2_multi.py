"""V2 multi-entity parity test (2v2, 3v3, 4v4 farmer-style fights).

Mirrors test_v2_parity.py methodology but supports N entities per team.
Each entity always-fires its weapon at the nearest alive enemy.

Usage:
    python test_v2_multi.py --per-team 2 --seeds 50
    python test_v2_multi.py --per-team 4 --seeds 20
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


# Choose cells in a 18x18 diamond so entities are spread out and within
# pistol range of at least one enemy.
def cells_for_team(team_id: int, n: int):
    """Return n distinct cell ids for team `team_id` (0 or 1).

    Spread entities across DIFFERENT rows so they don't block each
    other's LoS with the default pistol straight-line attack.
    """
    if team_id == 0:
        base = [(3, 0), (3, 2), (3, -2), (3, 4)]
    else:
        base = [(9, 0), (9, 2), (9, -2), (9, 4)]

    def cell_id(x, y):
        return x * 18 + 17 * y

    return [cell_id(x, y) for (x, y) in base[:n]]


def run_v2(seed: int, weapon_id: int, per_team: int,
            agility: int = 0, max_turns: int = 30) -> tuple:
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
    cells_a = cells_for_team(0, per_team)
    cells_b = cells_for_team(1, per_team)
    # TYPE_TEAM mode: 2 entities share team_id (1 for A, 2 for B), each
    # entity has its own farmer for log accounting.
    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    fid = 1
    for i in range(per_team):
        eng.add_farmer(fid, f"A{i+1}", "fr")
        eng.add_entity(team=0, fid=fid, name=f"A{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=agility,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=fid, team_id=1, weapons=[weapon_id], cell=cells_a[i])
        fid += 1
    for i in range(per_team):
        eng.add_farmer(fid, f"B{i+1}", "fr")
        eng.add_entity(team=1, fid=fid, name=f"B{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=agility,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=fid, team_id=2, weapons=[weapon_id], cell=cells_b[i])
        fid += 1
    eng.set_seed(seed)
    eng.set_max_turns(max_turns)
    eng.set_type(2)  # TYPE_TEAM
    eng.set_custom_map(obstacles={}, team1=cells_a, team2=cells_b)

    def cell_xy(cid):
        # 18x18 diamond: cid = x*18 + 17*y inverse-lookup not trivial,
        # but cell coords are stored on the engine side. We compare via
        # Manhattan distance using cell ids interpreted as positions.
        # Easier: use the engine's own getNearestEnemy semantics by
        # ranking via Manhattan(cell_a, cell_b) using map coordinates.
        # We don't have a binding for cell.x/y -- approximate via cell_id
        # which encodes x*18 + 17*y. For our test cells (small, same
        # diamond), Manhattan distance computed from the diamond formula:
        #   x = (cid - 17 * y) / 18  ; y is implicit. We brute-force by
        # computing dx = (cid_a - cid_b)//18, dy ~ residual.
        return cid

    def manhattan(ca, cb):
        # Diamond cell id encodes x*18 + 17*y. Decompose:
        # If both have y=0 (most common for our spread cells), dx = (ca-cb)/18
        # General: try all candidate y values and pick the smallest Manhattan.
        best = 10**9
        for ya in range(-9, 10):
            for yb in range(-9, 10):
                xa_num = ca - 17 * ya
                xb_num = cb - 17 * yb
                if xa_num % 18 != 0 or xb_num % 18 != 0: continue
                xa, xb = xa_num // 18, xb_num // 18
                d = abs(xa - xb) + abs(ya - yb)
                if d < best: best = d
        return best

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        # Pick nearest enemy (matches Python's getNearestEnemy).
        target = None
        target_dist = 10**9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue
            d = manhattan(my_cell, eng.entity_cell(i))
            if d < target_dist:
                target_dist = d
                target = i
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


def run_py(seed: int, weapon_id: int, per_team: int,
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
                if w is None:
                    return
                me.setWeapon(w)
            enemy_id = fight_class.getNearestEnemy(ai)
            if enemy_id < 0:
                return
            enemy = ai.getFight().getEntity(enemy_id)
            for _ in range(8):
                w = me.getWeapon()
                if w is None or me.getTP() < w.getCost():
                    break
                if not ai.getState().getMap().canUseAttack(me.getCell(), enemy.getCell(), w.getAttack()):
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
        e.strength = 200; e.agility = agility
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100; e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [weapon_id]; e.chips = []
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
        e.strength = 200; e.agility = agility
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100; e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [weapon_id]; e.chips = []
        e.ai_function = ScriptedAgent()
        sc.addEntity(1, e)
        fid += 1

    g = Generator()
    out = g.runScenario(sc, None, _NoReg(), _NoStats())

    actions = []
    for action in out.fight.actions:
        try:
            actions.append(action.getJSON())
        except Exception:
            pass
    return actions, out.winner


def diff_streams(a_norm, b_norm):
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
        "a_only_types": a_only, "b_only_types": b_only,
        "a_at_div": a_norm[first] if 0 <= first < len(a_norm) else None,
        "b_at_div": b_norm[first] if 0 <= first < len(b_norm) else None,
    }


def load_weapons():
    with open(os.path.join(PY_DIR, "data", "weapons.json")) as f:
        data = json.load(f)
    by_item = {int(spec["item"]): spec for spec in data.values()}
    return by_item


WEAPONS_JSON_BY_ITEM = load_weapons()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=20)
    p.add_argument("--per-team", type=int, default=2)
    p.add_argument("--weapon", type=int, default=37)
    p.add_argument("--agility", type=int, default=0)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    raw = WEAPONS_JSON_BY_ITEM[args.weapon]
    print("=" * 70)
    print(f" V2 multi-entity parity ({args.per_team}v{args.per_team})")
    print("=" * 70)
    print(f" weapon: {args.weapon} ({raw['name']})  agility={args.agility}")
    print(f" running {args.seeds} seeds...\n")

    n_ok = 0
    failures = []
    for seed in range(1, args.seeds + 1):
        try:
            v2_stream, v2_winner = run_v2(seed, args.weapon, args.per_team, args.agility)
            py_stream, py_winner = run_py(seed, args.weapon, args.per_team, args.agility)
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
                print(f"  seed {seed:>3}: DIVERGE @ idx {d['first_div']}")

    print()
    print(f" RESULTS: {n_ok}/{args.seeds} identical")
    for f in failures:
        print(f"  seed {f['seed']}: idx {f['first_div']}")
        print(f"    v2 ({f['a_n']}, w={f['v2_winner']}): {f['a_at_div']}")
        print(f"    py ({f['b_n']}, w={f['py_winner']}): {f['b_at_div']}")
        if f['a_only_types']:
            print(f"    v2_only: {f['a_only_types']}")
        if f['b_only_types']:
            print(f"    py_only: {f['b_only_types']}")


if __name__ == "__main__":
    main()
