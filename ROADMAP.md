# Full C Engine Roadmap

This document tracks the work remaining to make the C engine
byte-for-byte equivalent to the Java reference (and to the Python
port that is already validated against Java).

For AlphaZero-quality self-play, all of the runtime path must run
in C — Python only for scenario loading and NN scoring.

## Scope summary

| Layer | Lines (Python) | Status | Tests |
|---|---|---|---|
| Map / Cell / Topology | ~300 | ✅ done | — |
| RNG (Java LCG) | ~30 | ✅ done | 6 |
| Pathfinding (A* / BFS) | ~200 | ✅ done | — |
| Line of sight | ~80 | ✅ done | — |
| Basic actions (END/SET_WEAPON/MOVE) | ~50 | ✅ done | — |
| `legal_actions` enum | ~80 | ✅ done | — |
| Feature extraction (MLP) | ~150 | ✅ done | — |
| Damage formula + criticals | ~150 | ✅ done | 16 |
| Heal + Shields (abs/rel/raw) | ~100 | ✅ done | 4 |
| Buffs (8 stats, science-scaled) | ~100 | ✅ done | 3 |
| Raw buffs (9 stat slots, no scaling) | ~100 | ✅ done | 2 |
| Shackles (6 stat slots, magic-scaled) | ~100 | ✅ done | 2 |
| Vulnerabilities (negative shields) | ~50 | ✅ done | 2 |
| Vitality / NovaVitality / RawHeal | ~80 | ✅ done | 6 |
| NovaDamage / LifeDamage / Aftereffect | ~120 | ✅ done | 7 |
| Poison + tick framework | ~100 | ✅ done | 4 |
| Critical hit roll (agility/1000) | ~30 | ✅ done | 6 |
| Effect-storage framework | ~150 | ✅ done | 9 |
| Erosion (max-HP reduction) | ~50 | ✅ done | 6 |
| Kill / AddState / StealAbsoluteShield | ~50 | ✅ done | 5 |
| RemoveShackles / Antidote / MultiplyStats | ~80 | ✅ done | 5 |
| Debuff / TotalDebuff (effect reducers) | ~50 | ✅ done | 2 |
| Movement (Push/Attract/Slide/Teleport/Permutation) | ~100 | ✅ done | 9 |
| AoE shapes (mask + dynamic) | ~250 | ✅ done | 18 |
| Effect dispatcher (Effect.createEffect) | ~150 | ✅ done | 9 |
| Attack pipeline (Attack.applyOnCell) | ~200 | ✅ done | 6 |
| Turn driver (start/end + tick) | ~150 | ✅ done | 11 |
| Round driver (next entity + reset) | ~50 | ✅ done | included |
| Winner detection (alive teams + HP tiebreak) | ~30 | ✅ done | 9 |
| Start order (StartOrder.compute) | ~80 | ✅ done | 4 |
| **Action handler integration** | ~50 | ⏳ pending | — |
| **Action stream output (JSON log)** | ~300 | ❌ TODO | — |
| **Generator / scenario loading** | ~150 | ❌ TODO | — |
| **Java parity gate (golden fixtures)** | n/a | ❌ TODO | — |
| **Passive effects (POISON_TO_*, DAMAGE_TO_*, etc.)** | ~100 | ❌ TODO | — |
| **Resurrect / Summon (entity allocation)** | ~80 | ❌ TODO | — |

Test count: 20 binaries, ~150+ verified parity cases.

## Plan, week by week

### Week 1: Core damage parity ✅

- ✅ Day 1: Effect base struct + Effect.createEffect skeleton.
- ✅ Day 2: EffectDamage byte-for-byte.
- ✅ Day 3: Heal + AbsoluteShield + RelativeShield.
- ✅ Day 4: Buffs + Aftereffect + Poison.
- ✅ Day 5: Critical hit roll + parity tests.
- ✅ Day 6: Effect-storage framework.
- ✅ Day 7: Vitality + Shackles + Debuffs etc. (13 more types).
- ✅ Day 8: AoE shapes (11 mask + 4 dynamic).
- ✅ Day 9: Erosion helper.
- ✅ Day 10: Raw buffs / vulnerabilities / kill / etc. (8 more types).

### Week 2: Effect catalog + drivers ✅

- ✅ Day 1: Movement effects (Push/Attract/Slide/Teleport/Permutation).
- ✅ Day 2: Turn driver (start_turn tick + end_turn cleanup).
- ✅ Day 3: Win-condition detection.
- ✅ Day 4: Effect-creation wrapper (lw_effect_create dispatch).
- ✅ Day 5: Multiply-stats + remaining catalog effects.
- ✅ Day 6: Apply attack pipeline (lw_apply_attack_full).
- ✅ Day 7: Start order + Round driver.

### Week 3: Action stream + parity gate ⏳

- Day 1: Replace USE_WEAPON / USE_CHIP stubs in lw_action.c with the
  byte-for-byte attack pipeline (catalog lookup + dispatch).
- Day 2: Action-stream output (JSON-equivalent log lines that mirror
  the Python engine's Generator.runScenario format).
- Day 3: Generator.runScenario equivalent — load scenario JSON, drive
  fight to completion, dump action stream.
- Day 4: Java parity gate — replay 100 golden fights from
  compare/compare_many.py, byte-for-byte match.
- Day 5: Bisect any divergences; commit fixes.

### Week 4: AI integration + self-play ⏳

- Day 1: Cython API for runScenario (Python feeds JSON, gets stream).
- Day 2: Replace AI's beam_search to use the C state end-to-end.
- Day 3: Self-play data-gen loop in pure C.
- Day 4: Bench: 10K farmer fights through C engine.
- Day 5: Final benchmarks + tune + ship.

## Off-ramps

If the parity gate (Week 3 Day 4) reveals a stubborn divergence:

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
- [ ] Update this ROADMAP.md status table.
