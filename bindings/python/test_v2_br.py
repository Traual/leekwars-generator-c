"""V2 10-player BATTLE ROYALE parity (TYPE_BATTLE_ROYALE).

10 entities, each its own team/farmer, free-for-all to the death.
AI: pick nearest enemy, cast damage chip, fire weapon. No move
(see test_v2_team_real.py --no-move for the same isolation choice).

Usage:
    python test_v2_br.py --seeds 5
"""
from __future__ import annotations
import argparse, json, os, sys
import leekwars_c._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path: sys.path.insert(0, PY_DIR)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_v2_summon as base

WEAPON_ID = 37
CHIP_ID   = 5


def cells_for_br(n: int):
    """Spread n entities around a circle on an 18x18 diamond."""
    # Spread cells across the map; each in its own row/col so spawn cells
    # don't collide. Hand-picked to ensure all are valid and well-separated.
    base_xy = [
        (2, 0), (3, 3), (4, -3), (5, 4),
        (10, 0), (9, 3), (8, -3), (7, 4),
        (6, 5), (6, -5),
    ]
    return [x * 18 + 17 * y for (x, y) in base_xy[:n]]


def run_v2(seed: int, n_players: int = 10, max_turns: int = 25):
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
    cells = cells_for_br(n_players)
    # In TYPE_BATTLE_ROYALE the engine treats every entity as its own team.
    # We still need to register a team per entity so the placement logic
    # picks the right cell.
    for i in range(n_players):
        eng.add_team(i + 1, f"T{i+1}")
        eng.add_farmer(i + 1, f"P{i+1}", "fr")
    for i in range(n_players):
        eng.add_entity(team=i, fid=i + 1, name=f"P{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=0,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=i + 1, team_id=i + 1, weapons=[WEAPON_ID],
                       chips=[CHIP_ID], cell=cells[i])
    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_type(3)  # TYPE_BATTLE_ROYALE
    # Custom map: BR placement -- pass each cell as its own "team".
    # The engine accepts team1+team2 lists; for BR we collapse into a flat
    # list. set_custom_map signature only supports team1/team2, so we use
    # team1 = all cells, team2 = [] -- the entities still get placed by
    # initial_cell because we set has_cell=1 above.
    eng.set_custom_map(obstacles={}, team1=cells, team2=[])

    def my_ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        target = None; bd = 10**9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue  # in BR, teams differ
            d = eng.cell_distance2(my_cell, eng.entity_cell(i))
            if d < bd: bd = d; target = i
        if target is None: return 0
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
    return eng.stream_dump(), eng.winner


def run_py(seed: int, n_players: int = 10, max_turns: int = 25):
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
            try:
                chip_class.useChipOnCell(ai, CHIP_ID, enemy.getCell().getId())
            except Exception: pass
            for _ in range(8):
                w = me.getWeapon()
                if w is None or me.getTP() < w.getCost(): break
                if not ai.getState().getMap().canUseAttack(me.getCell(), enemy.getCell(), w.getAttack()): break
                try: r = weapon_class.useWeapon(ai, enemy_id)
                except Exception: break
                if r <= 0 or enemy.isDead() or me.isDead(): break

    cells = cells_for_br(n_players)
    sc = Scenario()
    sc.seed = seed; sc.maxTurns = max_turns
    sc.type = PyState.TYPE_BATTLE_ROYALE
    sc.context = PyState.CONTEXT_TEST
    sc.map = {"id": 0, "obstacles": {}, "team1": cells, "team2": []}

    for i in range(n_players):
        f = FarmerInfo(); f.id = i + 1; f.name = f"P{i+1}"; f.country = "fr"
        sc.farmers[i + 1] = f
        t = TeamInfo(); t.id = i + 1; t.name = f"T{i+1}"
        sc.teams[i + 1] = t
        e = EntityInfo()
        e.id = i + 1; e.name = f"P{i+1}"; e.type = 0
        e.farmer = i + 1; e.team = i + 1
        e.level = 100; e.life = 2500
        e.strength = 200; e.agility = 0
        e.wisdom = 0; e.resistance = 0; e.science = 0; e.magic = 0
        e.frequency = 100; e.cores = 10; e.ram = 10
        e.tp = 14; e.mp = 6
        e.weapons = [WEAPON_ID]; e.chips = [CHIP_ID]
        e.ai_function = ScriptedAgent()
        sc.addEntity(i, e)

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
    p.add_argument("--seeds", type=int, default=5)
    p.add_argument("--players", type=int, default=10)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    print(f"V2 BATTLE ROYALE parity ({args.players} players, {args.seeds} seeds)")
    print(f"  weapon={WEAPON_ID} (pistol)  chip={CHIP_ID} (flame)\n")

    n_ok = 0; failures = []
    for sd in range(1, args.seeds + 1):
        try:
            v_stream, vw = run_v2(sd, args.players)
            p_stream, pw = run_py(sd, args.players)
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
                print(f"  seed {sd:>3}: OK n={an} winner v2={vw} py={pw}")
        else:
            failures.append((sd, f, an, bn, av, pv, vw, pw))

    print(f"\nRESULTS: {n_ok}/{args.seeds} byte-identical")
    for sd, f, an, bn, av, pv, vw, pw in failures[:3]:
        print(f"  seed {sd}: idx {f}")
        print(f"    v2 ({an}, w={vw}): {av}")
        print(f"    py ({bn}, w={pw}): {pv}")


if __name__ == "__main__":
    main()
