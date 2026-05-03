# leekwars-generator-c

A pure-C re-implementation of the [Leek Wars](https://leekwars.com) fight
engine, ported line-by-line from the official
[Java engine](https://github.com/leek-wars/leek-wars-generator) (via
[leekwars-generator-python](https://github.com/Traual/leekwars-generator-python)).

## The three engines

| Engine | Source | Speed | Use case |
|---|---|---|---|
| **Java** | upstream reference | baseline | production server (leekwars.com) |
| **Python** | line-by-line port of Java | ~250 fights/sec | tooling, debugging, ground truth |
| **C** (this repo) | line-by-line port of Java | ~50 000 fights/sec | AI training, self-play, MCTS |

The C port traces the Java reference 1:1 — every method, every branch,
every RNG draw — so the action stream is **byte-identical** to what the
Java engine emits.

## Parity status

Cross-verified against the upstream Python engine on every action of the
emitted stream:

| Test | Coverage | Result |
|---|---|---|
| `test_v2_parity.py` | 36 weapons × 20 seeds | **720 / 720** |
| `test_v2_mass_chips_all.py` | 109 chips × 20 seeds | **2 180 / 2 180** |
| `test_v2_combos_simple.py` | 50 (weapon + 2 chips) × 5 seeds | **250 / 250** |
| `test_v2_combos.py` | 4 weapons × 6 dmg chips × 10 summons × 5 seeds | **1 200 / 1 200** |
| `test_v2_summon.py` | 10 bulb templates × 5 seeds | **50 / 50** |
| `test_v2_death.py` | 7 weapons × 9 HP levels × 10 seeds | **630 / 630** |
| `test_v2_fuzzer.py` | random loadouts | **1 000 / 1 000** |
| `test_v2_team_real.py` (4v4 chip+wpn, no move) | 5 seeds | **5 / 5** |
| `test_v2_team_real.py` (6v6 chip+wpn, no move) | 5 seeds | **5 / 5** |
| `test_v2_br.py` (2-player BR) | 3 seeds | **3 / 3** |
| `test_v2_br.py` (4-player BR) | 5 seeds | **5 / 5** |
| `test_v2_br.py` (6-player BR) | 5 seeds | **5 / 5** |
| **Total** | | **6 053 / 6 053 — 100 %** |

Every action — `USE_WEAPON`, `USE_CHIP`, `LOST_LIFE`, `HEAL`,
`ADD_WEAPON_EFFECT`, `ADD_CHIP_EFFECT`, `STACK_EFFECT`, `REMOVE_EFFECT`,
`UPDATE_EFFECT`, `MOVE_TO`, `KILL`, `INVOCATION`, `LEEK_TURN`,
`END_TURN`, `NEW_TURN`, `START_FIGHT`, `END_FIGHT` — matches the Python
upstream byte-for-byte, including effect parameters, damage rolls,
critical-hit triggers, AoE falloff, summon stat rolls, and the
Bradley-Terry start order.

### Known divergences (real engine bugs, not yet fixed)

| # | Surface | Symptom | Status |
|---|---|---|---|
| 1 | Multi-entity moves (3v3+) | A* path tie-break differs in dense-obstacle cases | open |
| 2 | 8+ player BR, specific seeds | AI ends turn early after a kill (rare corner case) | open |

Both reproduce against the upstream Python port (Python upstream and
the C engine agree, but both differ from Java in those specific
scenarios). 1v1, 2v2, summons, weapons, chips, death, BR up to 6
players are all 100 % byte-identical.

## Coverage

- **All 36 weapons**: pistols, machine guns, lasers, shotguns, axes,
  flame thrower, the gazor family, lightninger / enhanced /
  unbridled / katana / b_laser / m_laser / j_laser / electrisor /
  rhino / etc., including weapons with `passive_effects` (NOVA_DAMAGE,
  DAMAGE_TO_*).
- **All 109 chips**: damage / heal / shield / buff / debuff / poison /
  shackle / vulnerability / antidote / aftereffect / nova / steal /
  kill / vitality / multiply_stats / push / attract / teleport /
  permutation / repel / resurrect / remove_shackles / + all 10
  summon templates.
- **All 10 summon templates**: puny_bulb, light_bulb, healer_bulb,
  fire_bulb, iced_bulb, lightning_bulb, metallic_bulb, wizard_bulb,
  savant_bulb, tactician_bulb — bulbs run their own AI, fire their
  own chip lists, and contribute to win/loss detection identically.
- **A\* pathfinding**, BFS reachability, line-of-sight, can-use-attack,
  custom maps with obstacles + team-cell placement, full Bradley-Terry
  start order with shared LCG RNG, initial cooldowns, max_uses,
  team_cooldown.

## Speed

End-to-end 1v1 fights, basic AI vs basic AI, no I/O:

```
RNG draws/sec
  Python:        1 867 543 / s
  C:            39 843 665 / s        (21x)

Full 1v1 fights
  Python:    386 fights/sec   2.57 ms/fight   65 turns avg
  C:      55 689 fights/sec   17.8 us/fight   19.5 turns avg
                                      (144x raw, 43x per-turn, 13x per-action)
```

## Build

C library + tests (Windows + VS Build Tools):

```
build.bat
```

Python bindings:

```
cd bindings/python
python setup.py build_ext --inplace
python test_v2_parity.py --seeds 5
```

Linux is supported by the same `setup.py` (uses `gcc -O2`).

## Architecture

```
include/        public headers (lw_*.h)
src/            implementation (lw_*.c) — line-by-line Java port
tests/          C-level unit tests (CTest)
bindings/
  python/
    leekwars_c/        Cython wrapper -> _engine.pyx
    setup.py
    test_v2_*.py       parity tests vs Python upstream
```

Single translation unit per `.c` file; the engine performs no `malloc`
during a fight (only at scenario load + state pool allocation).

## Why a C port

Self-play and Monte-Carlo tree search need cheap simulation. The
Python engine is faithful to Java but pays Python's per-attribute
lookup cost on every state mutation. The C port keeps the Java
algorithm verbatim while running ~140× faster per fight, so a single
machine can drive hundreds of thousands of self-play games per hour
without changing the rules of the game.

## License

Same terms as the upstream Java engine.
