# C engine v2 — line-by-line Java port

This directory contains a **complete rewrite** of the C engine, ported
1:1 from the canonical Java source at
`C:\Users\aurel\Desktop\leekwars_generator_python\java_reference\src\main\java\com\leekwars\generator\`.

The v1 engine (`src/`, `include/`) is kept untouched as a baseline.
v2 lives in parallel directories so we can switch back if needed.

## Why v2 exists

The v1 engine was built bottom-up from concept (action stream, RNG,
damage formula) and reached **1500/1500 byte-identical fights** for
simple weapons (pistol, odachi). But:

- effect logging (`ADD_EFFECT` / `REMOVE_EFFECT`) used a different field
  ordering than Java/Python upstream;
- `Map.generateMap` was bypassed via custom_map monkey-patches in the
  parity test;
- chip dispatch fell back to a stub for unknown effect types;
- summons / movement / many edge cases were untested.

Patching v1 risks introducing subtle divergences. v2 takes the
opposite approach: *literally* translate the Java source line by line,
keeping the same names / comments / control flow / RNG draw order.

## Conventions

See `PORTING_CONVENTIONS.md` in this directory.

## Layout

```
include_v2/
  lw_constants.h        all shared constants (action ids, effect types,
                        stat indices, entity states, modifiers, ...)
  lw_action_stream.h    LwActionLog (8 ints + extra) and LwActionStream
  lw_actions.h          one emit function per Java ActionXxx
  lw_area.h             LwArea + dispatch over 17 Area subclasses
  lw_attack.h           LwAttack (applyOnCell, getTargetCells, ...)
  lw_cell.h             LwCell struct + accessors
  lw_chip.h             LwChip + Chips registry
  lw_effect.h           LwEffect base + lifecycle (createEffect, addLog)
  lw_effect_params.h    LwEffectParameters (id, value1, value2, turns,
                                            targets, modifiers)
  lw_entity.h           LwEntity (one struct for all entity kinds)
  lw_entity_info.h      LwEntityInfo (scenario-time entity config)
  lw_fight.h            LwFight (turn loop)
  lw_generator.h        LwGenerator (runScenario entry point)
  lw_item.h             LwItem + LwItemTemplate
  lw_leek.h             LwLeek (extends LwEntity)
  lw_map.h              LwMap + generateMap + getRandomCell + ...
  lw_obstacle_info.h    Obstacle metadata table
  lw_order.h            LwOrder (rolling turn order)
  lw_outcome.h          LwOutcome (winner + action stream + duration)
  lw_pathfinding.h      lw_pathfinding_get_case_distance, in_line, ...
  lw_rng.h              Java LCG (1:1 with TestRng.java)
  lw_scenario.h         LwScenario (seed, max_turns, custom_map, entities)
  lw_start_order.h      LwStartOrder (probability-based first-team pick)
  lw_state.h            LwState (root struct; owns map, entities, ...)
  lw_statistics.h       Default no-op statistics manager
  lw_stats.h            LwStats (Java TreeMap<Integer,Integer> as int[])
  lw_team.h             LwTeam (entity list + alive / total life)
  lw_util.h             java_round, java_div, java_mod, signum, ...
  lw_weapon.h           LwWeapon + Weapons registry

src_v2/
  PORTING_CONVENTIONS.md
  README.md             this file
  lw_actions.c          all 28 emit-function bodies
  lw_area.c             all 17 area classes ported into a tag-dispatch
  lw_attack.c           Attack.applyOnCell + getTargetCells + ...
  lw_effect.c           Effect.createEffect + addLog + dispatch
  lw_effect_*.c         one .c per concrete subclass (.apply / .applyStartTurn)
  lw_entity.c           Entity getters / setters / lifecycle / hooks
  lw_fight.c            Fight.startFight loop + finishFight
  lw_generator.c        Generator.runScenario
  lw_leek.c             Leek (small extension over Entity)
  lw_map.c              Map.generateMap + getRandomCell + ...
  lw_obstacle_info.c    obstacle table
  lw_order.c            rolling turn order
  lw_outcome.c          (small, just struct + accessors)
  lw_pathfinding.c      4 distance helpers
  lw_scenario.c         (small, just struct + accessors)
  lw_start_order.c      StartOrder.compute (RNG-driving)
  lw_state.c            State.init + useWeapon + useChip + ...
  lw_statistics.c       no-op default
  lw_team.c             team list + life sum
  lw_weapons.c          static Weapon registry
  lw_chips.c            static Chip registry
```

## Build

```
python bindings/python/setup_v2.py build_ext --inplace
```

The Cython binding lives in `bindings/python/leekwars_c_v2/_engine.pyx`.

## Test

The same parity test (`test_action_stream_strict.py`) is rewired to
target `leekwars_c_v2` once compilation succeeds. Goal: 100% strict
parity on every weapon + chip + summon scenario, no monkey-patches.
