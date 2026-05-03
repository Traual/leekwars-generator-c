"""Full-fight parity check: Python engine vs C engine (post-fix).

Drives identical scripted scenarios on both engines, then compares the
fight summary statistics:
  - winner team
  - final HPs of each entity
  - total damage dealt during the fight
  - fight length (turns)

Critical constraints to make this comparable:
  1. Same map dimensions (Python = 18x18 diamond, C = 18x18 diamond now)
  2. Same RNG seed
  3. Same agent (BasicAgent on Python side, basic_ai_c on C side)
  4. agility=0 to suppress crit-roll-based RNG drift

What we don't control yet (and so we tolerate):
  - Random obstacles: Python rolls 30-80 obstacles per fight via the
    same RNG (consumes draws from the same stream). C has none.
    -> entity start positions differ, fight can play out differently.
  - Chips: Python's make_solo_scenario gives chips. We disable them
    here via give_chips=False to match the C side.

Even with these constraints relaxed, both engines should give
statistically similar winner distributions across many seeds.

Usage:
    python test_full_parity.py             # 50 seeds, default
    python test_full_parity.py --seeds 100 # tighter stats
"""
from __future__ import annotations

import argparse
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Locate Python upstream
for _candidate in (
    os.environ.get("LEEKWARS_PY_DIR"),
    "C:/Users/aurel/Desktop/leekwars_generator_python",
    "/mnt/c/Users/aurel/Desktop/leekwars_generator_python",
):
    if _candidate and os.path.isdir(_candidate) and _candidate not in sys.path:
        sys.path.insert(0, _candidate)
        break

# Locate AI repo
for _candidate in (
    "C:/Users/aurel/Desktop/leekwars_ai_private",
    "/mnt/c/Users/aurel/Desktop/leekwars_ai_private",
):
    if _candidate and os.path.isdir(_candidate) and _candidate not in sys.path:
        sys.path.insert(0, _candidate)
        break


def run_c(seed: int):
    from ai.training.data_c import (
        setup_c_catalog, build_scenario_c, basic_ai_c, get_topo,
    )
    from leekwars_c._engine import WIN_ONGOING, WIN_DRAW

    weapon_ids, raw_specs = setup_c_catalog()
    s, profiles, my_weapons = build_scenario_c(seed, weapon_ids, raw_specs)
    _topo, W, _H = get_topo()
    s.stream_enable(True)
    s.stream_clear()

    for _ in range(64 * 8):
        active = s.next_entity_turn()
        if active < 0:
            break
        s.entity_start_turn(active)
        if s.entity_alive(active):
            basic_ai_c(s, active, profiles[active], W, my_weapons[active])
        if s.compute_winner() != WIN_ONGOING:
            break

    LOG_DAMAGE = 3
    stream = s.stream_dump()
    total_dmg = sum(int(e["v1"]) for e in stream if e["type"] == LOG_DAMAGE)
    return {
        "winner": s.compute_winner(),
        "hp": [s.entity_hp(0), s.entity_hp(1)],
        "turns": s.turn,
        "total_dmg": total_dmg,
    }


def run_py(seed: int):
    from leekwars.generator import Generator
    from leekwars.fight.fight import Fight as _Fight
    from leekwars.statistics.statistics_manager import DefaultStatisticsManager
    from ai.benchmarks.scenarios import make_solo_scenario
    from ai.agents.basic_ai import BasicAgent

    class _NoOpStats(DefaultStatisticsManager):
        def setGeneratorFight(self, fight): pass
    class _NoOpReg:
        def getRegisters(self, leek): return None
        def saveRegisters(self, leek, registers, is_new): pass

    sc = make_solo_scenario(seed, BasicAgent(), BasicAgent(), give_chips=False)
    g = Generator()
    captured = {"fight": None}
    _orig = _Fight.__init__
    def _patched(self, *a, **k):
        _orig(self, *a, **k); captured["fight"] = self
    _Fight.__init__ = _patched
    try:
        out = g.runScenario(sc, None, _NoOpReg(), _NoOpStats())
    finally:
        _Fight.__init__ = _orig

    fight = captured["fight"]
    if fight is None:
        return {"winner": -1, "hp": [-1, -1], "turns": 0, "total_dmg": 0}
    state = fight.getState()
    hps = [-1, -1]
    for ent in state.getEntities().values():
        idx = 0 if ent.getTeam() == 1 else 1
        hps[idx] = ent.getLife()

    # Sum LOST_LIFE damage entries
    total_dmg = 0
    for action in state.actions.actions:
        try:
            j = action.getJSON()
            if isinstance(j, list) and len(j) >= 4 and j[0] == 101:
                total_dmg += int(j[3])
        except Exception:
            pass

    # winner: out.winner is 0/1/-1 (team)
    return {
        "winner": (out.winner - 1) if out.winner in (1, 2) else -1,
        "hp": hps,
        "turns": state.turn if hasattr(state, "turn") else 0,
        "total_dmg": total_dmg,
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=50)
    args = p.parse_args()

    print("=" * 70)
    print(" Full-fight parity: Python upstream vs C engine")
    print("=" * 70)
    print(f" running {args.seeds} seeds on each engine...")
    print()

    c_team0 = c_team1 = c_draw = c_other = 0
    py_team0 = py_team1 = py_draw = py_other = 0
    c_dmgs = []; py_dmgs = []
    c_turns = []; py_turns = []

    t0 = time.perf_counter()
    for seed in range(1, args.seeds + 1):
        try:
            cr = run_c(seed)
        except Exception as e:
            print(f"   C error seed {seed}: {e}")
            continue
        try:
            pr = run_py(seed)
        except Exception as e:
            print(f"   Py error seed {seed}: {e}")
            continue

        if cr["winner"] == 0:   c_team0 += 1
        elif cr["winner"] == 1: c_team1 += 1
        elif cr["winner"] in (-2,): c_draw += 1
        else: c_other += 1
        c_dmgs.append(cr["total_dmg"])
        c_turns.append(cr["turns"])

        if pr["winner"] == 0:   py_team0 += 1
        elif pr["winner"] == 1: py_team1 += 1
        elif pr["winner"] == -1 and (pr["hp"][0] == 0 or pr["hp"][1] == 0):
            # one died but winner not picked (timeout, multi-team)
            pass
        else: py_other += 1
        py_dmgs.append(pr["total_dmg"])
        py_turns.append(pr["turns"])

    dt = time.perf_counter() - t0
    print(f" {args.seeds} seeds done in {dt:.1f}s")
    print()
    print(f" {'Metric':<28}  {'C engine':>14}  {'Python engine':>14}")
    print(f" {'-'*28}  {'-'*14}  {'-'*14}")
    print(f" {'team0 wins':<28}  {c_team0:>14}  {py_team0:>14}")
    print(f" {'team1 wins':<28}  {c_team1:>14}  {py_team1:>14}")
    print(f" {'draws/timeouts':<28}  {c_draw + c_other:>14}  {py_draw + py_other:>14}")
    if c_dmgs and py_dmgs:
        print(f" {'avg total damage / fight':<28}  "
              f"{sum(c_dmgs)/len(c_dmgs):>14.0f}  "
              f"{sum(py_dmgs)/len(py_dmgs):>14.0f}")
    if c_turns and py_turns:
        print(f" {'avg turns / fight':<28}  "
              f"{sum(c_turns)/len(c_turns):>14.1f}  "
              f"{sum(py_turns)/len(py_turns):>14.1f}")
    print()
    diff_winrate = abs((c_team0 - py_team0) / max(args.seeds, 1))
    if diff_winrate < 0.10:
        print(f" [OK] team0-winrate diff = {diff_winrate*100:.0f}% (within 10% tolerance)")
    else:
        print(f" [DIVERGE] team0-winrate diff = {diff_winrate*100:.0f}% (above 10% tolerance)")
    print(f" Note: exact identity isn't expected because Python rolls random")
    print(f"       obstacles + start positions; C uses fixed start cells.")


if __name__ == "__main__":
    main()
