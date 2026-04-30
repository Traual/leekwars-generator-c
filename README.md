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

| Component                            | Status |
|--------------------------------------|--------|
| State / Map / Entity structs         | ✅ done |
| State alloc + memcpy clone + pool    | ✅ done |
| RNG (LCG matching Java/Python)       | ✅ 6/6 byte-for-byte parity |
| A* pathfinding (LIFO tie-break)      | ✅ done |
| BFS-bounded reachability             | ✅ done |
| Line-of-sight + canUseAttack         | ✅ done |
| `legal_actions` enumeration          | ✅ done |
| `apply_action`: END / SET_WEAPON / MOVE | ✅ done |
| `apply_action`: USE_WEAPON / USE_CHIP | ⚠️ approximate (NOT Java-parity) |
| Effects (poison / shield / buff)     | ⚠️ partial (in chip apply only) |
| `extract_mlp_features` (zero-copy)   | ✅ done |
| Java parity gate on full fights      | ❌ TODO |
| Python (Cython) bindings             | ✅ done |

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

## Important caveat

The C engine is **not** a drop-in replacement for the Python /
Java engine. USE_WEAPON / USE_CHIP / effects use a deterministic
approximation good enough for AI scoring but not for byte-for-byte
fight reproduction. For training data generation you should still
run fights through the Python engine; the C engine is the AI's
fast search backend.

## License

Same terms as the upstream Java engine.
