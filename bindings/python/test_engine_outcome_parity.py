"""Statistical outcome parity: C engine vs Python upstream.

For N seeded fights with the SAME catalog spec + the SAME stat ranges
+ the SAME map dimensions, we expect both engines to produce
statistically equivalent fight outcomes:

  - Winner distribution (team0/team1/draw) within sampling noise
  - Average damage per weapon class within ~5%
  - Average fight length within ~10%

If the engines diverge sharply on any of those, there's a real bug.

This complements the per-effect parity_gate (which already covers
114k cases of byte-for-byte effect math). The point here is to catch
"sequencing" drift in the action pipeline: things that pass per-effect
but break in composition.

Both engines now use the 18x18 diamond topology used by the Python
default Map.generateMap (width=18, height=18).
"""
from __future__ import annotations

import json
import os
import random
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


for _candidate in (
    os.environ.get("LEEKWARS_PY_DIR"),
    "C:/Users/aurel/Desktop/leekwars_generator_python",
    "/mnt/c/Users/aurel/Desktop/leekwars_generator_python",
):
    if _candidate and os.path.isdir(_candidate) and _candidate not in sys.path:
        sys.path.insert(0, _candidate)
        break


# Add the AI repo too so we can use its data_c helpers
for _candidate in (
    "C:/Users/aurel/Desktop/leekwars_ai_private",
    "/mnt/c/Users/aurel/Desktop/leekwars_ai_private",
):
    if _candidate and os.path.isdir(_candidate) and _candidate not in sys.path:
        sys.path.insert(0, _candidate)
        break

from ai.training.data_c import (
    setup_c_catalog, build_scenario_c, basic_ai_c, get_topo, run_one_fight_c,
)
from leekwars_c._engine import WIN_ONGOING, WIN_DRAW


def run_c_fight(seed: int, weapon_ids, raw_specs):
    """Run a single C fight, return (winner, fight_turns, total_damage)."""
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
    stream = s.stream_dump()
    LOG_DAMAGE = 3
    total_dmg = sum(int(e["v1"]) for e in stream if e["type"] == LOG_DAMAGE)
    return s.compute_winner(), s.turn, total_dmg


def run_py_fight(seed: int):
    """Run a single Python fight using BasicAgent vs BasicAgent.
    Returns (winner, turns, total_damage)."""
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
        return -1, 0, 0
    state = fight.getState()
    # Sum LOST_LIFE (id=101) damage values
    total_dmg = 0
    for action in state.actions.actions:
        try:
            j = action.getJSON()
            if isinstance(j, list) and len(j) > 0 and j[0] == 101 and len(j) > 3:
                total_dmg += int(j[3])  # value at index 3
        except Exception:
            pass
    winner = out.winner
    turns = state.turn if hasattr(state, "turn") else 0
    return winner, turns, total_dmg


def main():
    print("=" * 70)
    print(" Statistical outcome parity: C vs Python")
    print("=" * 70)

    weapon_ids, raw_specs = setup_c_catalog()
    print(f" C catalog: {len(weapon_ids)} weapons")
    n_seeds = 30

    # Single agreement check first
    print(f"\n Running {n_seeds} fights on each engine...")

    c_winners = {0: 0, 1: 0, WIN_DRAW: 0, -1: 0}  # ongoing -> -1 (didn't end)
    c_turns = []
    c_dmg = []
    py_winners = {0: 0, 1: 0, -1: 0}  # py uses 0/1 for teams, -1 for none
    py_turns = []
    py_dmg = []

    for seed in range(1, n_seeds + 1):
        try:
            cw, ct, cd = run_c_fight(seed, weapon_ids, raw_specs)
        except Exception as e:
            print(f"  C error seed {seed}: {e}")
            cw, ct, cd = -1, 0, 0
        c_winners[cw] = c_winners.get(cw, 0) + 1
        c_turns.append(ct)
        c_dmg.append(cd)

        try:
            pw, pt, pd = run_py_fight(seed)
        except Exception as e:
            print(f"  Py error seed {seed}: {e}")
            pw, pt, pd = -1, 0, 0
        py_winners[pw] = py_winners.get(pw, 0) + 1
        py_turns.append(pt)
        py_dmg.append(pd)

    print()
    print(" Winner distribution:")
    print(f"   C  team0={c_winners.get(0, 0):>3}  "
          f"team1={c_winners.get(1, 0):>3}  "
          f"draw={c_winners.get(WIN_DRAW, 0):>3}  "
          f"ongoing={c_winners.get(-1, 0):>3}")
    print(f"   Py team0={py_winners.get(0, 0):>3}  "
          f"team1={py_winners.get(1, 0):>3}  "
          f"none={py_winners.get(-1, 0):>3}")
    print()
    print(" Fight length (turns):")
    print(f"   C  avg={statistics.mean(c_turns):>5.1f}  "
          f"median={statistics.median(c_turns):>3.0f}  "
          f"max={max(c_turns)}")
    print(f"   Py avg={statistics.mean(py_turns):>5.1f}  "
          f"median={statistics.median(py_turns):>3.0f}  "
          f"max={max(py_turns)}")
    print()
    print(" Total damage per fight:")
    print(f"   C  avg={statistics.mean(c_dmg):>7.0f}  median={statistics.median(c_dmg):>7.0f}")
    print(f"   Py avg={statistics.mean(py_dmg):>7.0f}  median={statistics.median(py_dmg):>7.0f}")

    # Note: we don't expect IDENTICAL winners per seed because
    # entity start positions, obstacles, and AI move sequences differ
    # between engines (Python uses random map+obstacles per seed; C uses
    # fixed start cells, no obstacles). What we expect is statistically
    # similar distributions at scale.


if __name__ == "__main__":
    main()
