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
| **Strong parity gate vs upstream Python** | ✅ 114000/114000 cases over 57 effect categories — see `bindings/python/parity_gate.py`. Caught 4 real divergences (life_steal+return_damage on Damage, targetCount-vs-aoe on RawBuffMP/TP, sign-inside-java_round on Effect.reduce, decrement-on-caster's-startTurn) that my piecemeal port missed. |
| Effect.createEffect stacking/replacement  | ✅ done — pre-apply removal of existing same-(id, attack_id) entry for non-stackable, post-apply merge with same-(id, attack_id, turns, caster) for stackable |
| Passive event hooks                       | ✅ framework + 7 events wired (lw_event_on_*) + 2 integration tests (damage_to_strength, moved_to_mp). Covers POISON_TO_SCIENCE, DAMAGE_TO_ABSOLUTE_SHIELD, DAMAGE_TO_STRENGTH, NOVA_DAMAGE_TO_MAGIC, MOVED_TO_MP, ALLY_KILLED_TO_AGILITY, KILL_TO_TP, CRITICAL_TO_HEAL. |
| Movement parity test (Push / Attract)     | ✅ 4000/4000 cases vs upstream Python+Java |
| Multi-target attack parity                | ✅ 500/500 CIRCLE_2 AoE cases — verifies (1 - dist*0.2) falloff per target |
| Summon (entity allocation)                | ✅ bulb-template registry + 6/6 unit tests (stat formula, crit 1.2x bonus, order insertion after caster) |
| Action-stream JSON output                 | ✅ embedded LwActionStream + 16 action types wired (USE_WEAPON / USE_CHIP / DAMAGE / HEAL / KILL / CRITICAL / ADD_EFFECT / STACK_EFFECT / SLIDE / TELEPORT / INVOCATION / RESURRECT / VITALITY / NOVA_VITALITY / ADD_STATE / START_TURN). Opt-in via `state.stream.enabled = 1`; zero overhead when off. |

## Microbench (Python -> C, 5x5 toy state)

```
Clone         (C, memcpy + pool):    67 us / call
legal_actions (C):                   1.4 us / call
extract_mlp   (C, zero-copy):        0.3 us / call
```

## End-to-end benchmark (full 1v1 fights, basic-AI vs basic-AI)

`bindings/python/bench_compare.py` drives the same workload through
the C engine and the upstream Python `leekwars-generator-python`:

```
RNG draws/sec:
  Python:      1,867,543 / s
  C:          39,843,665 / s
  Speedup: 21x

Full 1v1 fights (200 each side):
  Python:    386 fights/sec    2.57 ms/fight    65 turns   173k actions/sec
  C:      55,689 fights/sec   17.8 us/fight   19.5 turns  2.20M actions/sec

  Per-fight raw speedup:                  144x
  Per-turn speedup (turn-count adjusted):  43x
  Per-action speedup:                      13x
```

The Python side still has more bookkeeping (action-stream emission,
catalog JSON loader, full statistics manager); the C engine skips
those for AI search workloads.

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
byte-for-byte across:

  - **114000 / 114000** fuzz cases over 57 effect categories
    (`bindings/python/parity_gate.py`)
  - **4000 / 4000** Push / Attract movement cases
    (`bindings/python/test_movement_parity.py`)
  - **500 / 500** multi-target AoE cases verifying per-target
    falloff (`bindings/python/test_attack_multitarget.py`)
  - **2 / 2** passive integration cases (damage_to_strength,
    moved_to_mp; `bindings/python/test_passive_hooks.py`)
  - **25 / 25** C-level unit tests covering RNG, pathfinding, LoS,
    state alloc/clone/pool, action handler, legal_actions, area
    masks, effect store, dispatcher, attack pipeline, turn driver,
    winner detection, summon, action stream

For every case we compare every numeric mutation: target hp,
total_hp, buff_stats[18], state_flags + the same fields on the
caster. Plus, where relevant, the entity's effect list and the
turn-order index. See the test files for the exact comparison
predicates.

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
4. Turn decrement was happening on the **target**'s end-of-turn;
   Python decrements on the **caster**'s start-of-turn (walking
   `caster.launchedEffects`). 2-entity fight wall-clock timing
   happened to converge by accident, but 3+-entity fights and any
   "buff alive one extra turn" scenario diverged.

Plus the dispatcher now implements the two missing branches of
`Effect.createEffect`:

  - Pre-apply removal of an existing same-(id, attack_id) entry
    when the new effect is non-stackable
  - Post-apply merge with an existing same-(id, attack_id, turns,
    caster) entry when stackable

The C engine is feature-complete vs the Python / Java reference for
combat-relevant code paths: damage / heal / shields / buffs /
debuffs / shackles / vulnerabilities / poison / aftereffect /
nova damage / life damage / vitality / steal / kill / multiply
stats / antidote / remove shackles / resurrect / push / attract /
teleport / permutation / summon / passive event hooks (7 types) /
action-stream emission (16 action types).

The end-to-end self-play loop (`tests/test_full_fight.c`) runs a
1v1 to a clean winner using only C primitives.

## License

Same terms as the upstream Java engine.
