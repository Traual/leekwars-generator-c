# leekwars-generator-c

A pure-C re-implementation of the [Leek Wars](https://leekwars.com) fight
engine, ported from the Python translation at
[leekwars-generator-python](https://github.com/Traual/leekwars-generator-python),
itself a line-by-line port of the official
[Java engine](https://github.com/leek-wars/leek-wars-generator).

## Why

Running large-scale AI training (self-play, MCTS, etc.) needs the
engine to be cheap. The Python port is faithful to the Java engine
but every state operation pays Python's attribute-lookup tax. The
C port targets the AI's hot path — `clone`, `legal_actions`,
`extract_mlp_features` — where two-orders-of-magnitude speedups
unlock new training regimes.

## Status

Active development. The Python and Java engines are the references;
this C port is being built bottom-up alongside parity tests.

| Component                                 | Status |
|-------------------------------------------|--------|
| State / Map / Entity structs              | ✅ done |
| State alloc + memcpy clone + pool         | ✅ done |
| RNG (LCG matching Java/Python)            | ✅ 6/6 byte-for-byte parity |
| A* pathfinding (LIFO tie-break)           | ✅ done |
| BFS-bounded reachability                  | ✅ done |
| Line-of-sight + canUseAttack              | ✅ done |
| `legal_actions` enumeration               | ✅ done |
| `apply_action`: END / SET_WEAPON / MOVE   | ✅ done |
| `apply_action`: USE_WEAPON / USE_CHIP     | ✅ done (catalog-routed → byte-for-byte pipeline; falls back to deterministic stub if catalog empty) |
| Damage / Heal / Shield formulas           | ✅ 16+4 byte-for-byte cases |
| Critical-hit roll (agility/1000)          | ✅ 6/6 byte-for-byte cases |
| Erosion (max-HP reduction)                | ✅ 6/6 cases |
| AoE shapes (11 mask + 4 dynamic)          | ✅ 18/18 cases |
| Effect-storage framework                  | ✅ 9/9 cases |
| Effect dispatcher (`Effect.createEffect`) | ✅ 9/9 cases — ~35 effect types routed |
| Attack pipeline (`Attack.applyOnCell`)    | ✅ 6/6 cases |
| Movement (Push/Attract/Slide/TP/Permute)  | ✅ 9/9 cases |
| Turn driver + Round driver                | ✅ 11/11 cases |
| Start order (StartOrder.compute)          | ✅ 4/4 byte-for-byte cases |
| Winner detection (alive teams + HP)       | ✅ 9/9 cases |
| `extract_mlp_features` (zero-copy)        | ✅ done |
| Python (Cython) bindings                  | ✅ done (legacy primitives; new ones unwired) |
| **Strong parity gate vs upstream Python** | ✅ 108000/108000 cases over 54 effect categories — see `bindings/python/parity_gate.py`. Caught 3 real divergences (life_steal+return_damage on Damage, targetCount-vs-aoe on RawBuffMP/TP, sign-inside-java_round on Effect.reduce) that my piecemeal port missed. |
| Action-stream JSON output                 | ❌ TODO |
| Resurrect / Summon (entity allocation)    | ❌ TODO |
| Passive event hooks (POISON_TO_*, etc.)   | ❌ TODO |
| Effect.createEffect stacking/replacement  | ❌ TODO (dispatcher just appends; no merge for stackable, no replace for non-stackable) |

## Microbench (Python -> C, 5x5 toy state)

```
Clone         (C, memcpy + pool):    68 us / call
legal_actions (C):                   1.4 us / call
extract_mlp   (C, zero-copy):        0.3 us / call

For comparison the Python reference engine is:
Clone:                              ~150 us / call
legal_actions:                      ~1500 us / call (post-optim)
extract_mlp:                        ~100 us / call (Cython entity_token)

Speedup on the AI's per-state inner loop: ~25x.
```

For a beam-search turn (3000 states evaluated):

```
Pure Python : ~5 200 ms
C inner loop:   ~210 ms
```

## Architecture

Three layers:

1. **C engine** (`src/`, `include/`) -- pure C99, no malloc inside a
   fight (only at scenario load + state pool). Single-translation-unit-
   friendly; static library or directly statically linked into the
   Python extension.

2. **Python bindings** (`bindings/python/`) -- Cython wrapper exposing
   `State`, `Action`, `Topology`, `InventoryProfile`. State.clone()
   uses memcpy + pool; legal_actions returns a list of Actions;
   extract_mlp_features writes to a numpy view (zero-copy).

3. **AI integration** (in the consumer repo, e.g. `leekwars-ai-private`)
   -- the AI's `beam_search` keeps a C State alongside the Python
   State; per turn it pushes the Python state into C, runs the inner
   loop in C, picks an action, and applies it via Python (which
   stays the canonical Java-parity executor).

## Build

C library + tests (Windows + VS 2026 Build Tools):

```
build.bat
```

Python bindings:

```
cd bindings/python
build_python.bat
python bench_clone.py
```

## Layout

```
include/        public headers (lw_*.h)
src/            implementation (lw_*.c)
tests/          C-level unit + parity tests
bindings/
  python/       Cython wrapper exposing State / Action / etc.
```

## Parity status

The C engine matches the upstream Python `leekwars` engine
byte-for-byte across **54 effect categories × 2000 random parameter
sets = 108000 fuzz cases**, comparing every numeric mutation it
makes (target hp, total_hp, buff_stats[18], state_flags + same
fields on caster). See `bindings/python/parity_gate.py`.

Real bugs the gate caught and we fixed:

1. `EffectDamage.apply` was missing two whole branches —
   `lifeSteal` (caster heals from `value * caster.wisdom / 1000`)
   and `returnDamage` (target's `damage_return` reflects damage to
   caster). Now ported in `lw_apply_damage_v2`.
2. `EffectRawBuffMP` and `EffectRawBuffTP` use `targetCount` as the
   multiplier, not `aoe` like the other 9 raw buffs. Dispatcher
   was passing the wrong arg.
3. `Effect.reduce` rounds the **signed** value (`abs * factor *
   sign` inside `java_round`), not `abs * factor` then `* sign`.
   Off-by-one on .5 boundaries for negative shackles.

What's still missing for full Java parity:

  - **Action-stream JSON output** so we can diff C vs. Python
    event-by-event over a full scripted fight
  - **Resurrect / Summon** (entity-allocation path)
  - **Passive event hooks** for POISON_TO_*, DAMAGE_TO_*,
    KILL_TO_TP, ALLY_KILLED_TO_AGILITY, etc.
  - **Effect.createEffect stacking / replacement** semantics

The end-to-end self-play loop (`tests/test_full_fight.c`) runs a
1v1 to a clean winner using only C primitives.

## License

Same terms as the upstream Java engine.
