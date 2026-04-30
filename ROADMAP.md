# Full C Engine Roadmap

This document tracks the work remaining to make the C engine
byte-for-byte equivalent to the Java reference (and to the Python
port that is already validated against Java). The current state
covers structs, RNG, pathfinding, LoS, basic actions, and feature
extraction; it does **not** yet cover damage, effects, turn loop, or
action stream output.

For AlphaZero-quality self-play, all of that must be ported.

## Scope summary

| Layer | Lines (Python) | Status |
|---|---|---|
| Map / Cell / Topology | ~300 | ✅ done |
| RNG | ~30 | ✅ done |
| Pathfinding (A* / BFS) | ~200 | ✅ done |
| Line of sight | ~80 | ✅ done |
| Basic actions (END/SET_WEAPON/MOVE) | ~50 | ✅ done |
| `legal_actions` enum | ~80 | ✅ done |
| Feature extraction (MLP) | ~150 | ✅ done |
| **Damage formula + criticals** | ~150 | ⚠️ approx only |
| **Effect framework + types** | ~600 | ⚠️ partial (4 of ~30 types) |
| **Stats arithmetic + erosion** | ~100 | ❌ TODO |
| **AoE shapes (laser/circle/plus/X/first-in-line)** | ~250 | ❌ TODO |
| **Turn loop (`Fight.startFight`)** | ~200 | ❌ TODO |
| **Generator / scenario loading** | ~150 | ❌ TODO |
| **Action stream output (JSON-equivalent)** | ~300 | ❌ TODO |
| **Java parity gate (golden fixtures)** | n/a | ❌ TODO |

Total remaining: ~1750 lines of Python to port + tests.

## Plan, week by week

### Week 1: Core damage parity

Goal: a simple `weaponX -> entityY` use yields the same final HP as
the Python engine.

- Day 1: Effect base class in C + `Effect.createEffect` skeleton.
- Day 2: `EffectDamage.apply` byte-for-byte (RNG roll, strength mult,
  resistance, shields, erosion).
- Day 3: `EffectHeal.apply` + `EffectAbsoluteShield.apply` +
  `EffectRelativeShield.apply`.
- Day 4: Critical hit factor application; `Entity.onCritical()`.
- Day 5: Parity test harness — given a scenario JSON + sequence of
  actions, compare final HP across all entities.

### Week 2: Full effects + AoE

- Day 1: AoE shapes (`Area.getArea`): SINGLE, CIRCLE_1/2/3, LASER,
  PLUS_1/2/3, X_1/2.
- Day 2: FIRST_IN_LINE area + LoS interaction.
- Day 3: Effect ticking each turn (`Effect.tick` + `Effect.endTurn`).
- Day 4: Buff effects (BUFF_STRENGTH / AGILITY / WISDOM / RES / etc.)
  including stat-change side effects.
- Day 5: Poison decay, damage erosion model.

### Week 3: Turn loop + parity gate

- Day 1: `Order` (initiative + turn rotation) in C.
- Day 2: `Fight.startFight` / per-turn flow / win condition check.
- Day 3: Action stream output (write the same JSON-shaped log lines
  the Python engine emits).
- Day 4: `Generator.runScenario` equivalent — load JSON, run, return
  outcome.
- Day 5: Wire Java parity gate. Use the existing `compare/compare_many.py`
  fixtures: same seeds, same action streams, byte-for-byte match.

### Week 4: AI integration + self-play

- Day 1: Cython API for `runScenario` (Python loader feeds JSON to C,
  gets full action stream back).
- Day 2: Replace AI's beam_search to use the new C State end-to-end
  (no Python state alongside).
- Day 3: Self-play data-gen loop in pure C (calls back to Python only
  for NN scoring).
- Day 4: Bench: 10K farmer fights through C engine, compare to
  current Python wallclock.
- Day 5: Tune; commit benchmarks; ship.

## Off-ramps

If at any point the parity gate (Week 3 Day 5) reveals a stubborn
divergence, two options:

1. **Hybrid mode**: keep the C inner loop (clone, legal_actions,
   features) but route action application through Python. Lose ~3-5x
   speedup vs full C, but no parity work needed.
2. **Bisect divergence**: log the first action where C and Python
   differ, narrow to a single effect / formula, fix, retry.

## Repo conventions

- All damage / effect code goes in `src/lw_*.c` with matching headers.
- Tests live in `tests/test_*.c` (C-level) and
  `bindings/python/test_parity_*.py` (Python-driven golden tests).
- Every commit must keep the C unit tests passing.
- Java parity gate runs in CI once it exists.

## Checklist

Use this when working through a port:

- [ ] Read the Python reference (and follow imports until the leaf).
- [ ] Port to C with the same arithmetic order (matters for RNG sync).
- [ ] Add a unit test against expected output from a Python prompt.
- [ ] Bench the new path; flag if it's slower than the previous.
- [ ] Update README status table.
