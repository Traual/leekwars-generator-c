# leekwars-generator-c

A pure-C re-implementation of the [Leek Wars](https://leekwars.com) fight
engine, ported from the Python translation at
[leekwars-generator-python](https://github.com/Traual/leekwars-generator-python),
which itself is a line-by-line port of the official
[Java engine](https://github.com/leek-wars/leek-wars-generator).

## Why

Running large-scale AI training (self-play, MCTS, etc.) needs the
engine to be cheap. The Python port is already faithful to the Java
engine, but state-cloning a fight in Python costs ~150 microseconds
even after tight optimisation.

This C engine targets:

- **Byte-for-byte parity** with the Java engine on action streams.
- **State clone via a single `memcpy`** (~5-15 µs).
- **Predictable memory layout**: every entity, effect, and cell lives
  in a fixed-size array; no hidden allocations during a fight.
- **Embedding-friendly**: stable ABI for Python (Cython) wrappers.

## Status

🚧 **Early development.** The Python and Java engines are the references;
this C port is being built bottom-up alongside parity tests.

| Component               | Status |
|-------------------------|--------|
| State / Map / Entity    | scaffolded |
| RNG (Java LCG)          | scaffolded |
| A* / BFS pathfinding    | TODO   |
| Line-of-sight           | TODO   |
| Action enum + apply     | TODO   |
| Damage / effects        | TODO   |
| Java parity tests       | TODO   |
| Python (Cython) bindings| TODO   |

## Layout

```
include/        public headers (lw_*.h)
src/            implementation (lw_*.c)
tests/          C-level unit + parity tests
bindings/
  python/       Cython wrapper exposing State / Action / etc. to Python
```

## Build

Native C library (when ready):

```
cmake -S . -B build && cmake --build build
```

Python bindings (when ready):

```
pip install -e .
```

## License

Same terms as the upstream Java engine.
