# v1 vs v2 — comparaison du moteur C Leek Wars

Ce repo contient **deux implémentations C** du moteur Leek Wars :
- **v1** (`src/` + `include/`) — port itératif, primitive par primitive, validé via tests unitaires
- **v2** (`src_v2/` + `include_v2/`) — port from-scratch ligne par ligne du Java, validé via streams d'action complets

Les deux compilent ensemble actuellement. Cette note explique les différences pour décider laquelle garder.

## TL;DR

| Critère | v1 | v2 |
|---|---|---|
| **Approche** | Port primitive-par-primitive avec tests unitaires | Port from-scratch ligne-par-ligne du Java |
| **LOC C** | ~10k | ~19k |
| **Surface API exposée au binding** | Search MCTS / beam, legal_actions, extract_mlp_features | Engine.run_scenario style Java direct |
| **Test de parité** | 114000/114000 cases primitives vs Python | 10500+ trials full-fight byte-identical vs Python |
| **Couverture** | Effets / formulas / passive hooks isolés | Fights end-to-end avec actions arbitraires |
| **Idiome** | C optimisé, structures plates, MCTS-friendly | C qui mirroir le Java (méthodes statiques, ordre d'opérations) |

## Ce que chaque version fait bien

### v1 forte sur

- **AI training hot path** : `clone()` rapide (memcpy ~5-15µs), `legal_actions()` enumeration, `extract_mlp_features()` zero-copy
- **Tests primitives** : 114000 cas effect-by-effect prouvent les formules une par une
- **Optimisé pour MCTS** : structures dataclass-like, pas de heap dans le hot path
- **6157 fights/sec** mesuré (vs 256 pour Python upstream)

### v2 forte sur

- **Fidélité Java garantie** : code C structurellement isomorphe au Java source — chaque méthode Java a sa fonction C, dans le même ordre, mêmes branches
- **Test de parité end-to-end** : valide le STREAM D'ACTIONS complet (USE_WEAPON, DAMAGE, MOVE_TO, USE_CHIP, ADD_EFFECT, REMOVE_EFFECT, etc.) byte-pour-byte sur des fights complets de 30 turns
- **Couverture exhaustive** :
  - 109 chips testés (99% byte-identical)
  - 36 weapons testés (94% byte-identical)
  - 10 bulb (summons) byte-identical
  - 1v1, 2v2, 3v3, 4v4
  - Random maps avec obstacles
  - Movement + A* + path
  - Critical hits (agility>0)
  - Random fuzzer (random IA: 2000 fights byte-identical)
- **Engine.add_chip / add_summon_template** : peuvent créer un Bulb summon end-to-end avec son AI Bulb

## Bug surface

### v1 : 4 vraies divergences corrigées par le parity gate
- life_steal + return_damage sur Damage
- targetCount-vs-aoe sur RawBuffMP/TP
- sign-inside-java_round sur Effect.reduce
- decrement-on-caster's-startTurn

Ces bugs ont été détectés via le port piecemeal et corrigés. Bon signe que v1 a été stress-testé.

### v2 : connue à corriger
- Chip 68 (inversion, PERMUTATION + HEAL + VULNERABILITY) : v2 accepte le cast à idx=2, py refuse → 1 chip sur 109 (5 trials sur 545)
- 2 weapons self-cast (unbridled_gazor, enhanced_lightninger) : test AI tire sur ennemi au lieu de soi-même → setup test, pas bug engine

Le reste du moteur v2 est byte-identical.

## Architecture

### v1 (`src/` + `include/`)
```
include/
  lw_state.h         -- LwState struct (entities[], map, rng, action stream)
  lw_action.h        -- Action discrete (USE_WEAPON, MOVE, etc.)
  lw_legal.h         -- legal_actions() enumeration
  lw_features.h      -- extract_mlp_features() for NN input
  lw_attack_apply.h  -- byte-for-byte attack pipeline
  ...
src/
  lw_state.c, lw_attack_apply.c, lw_damage.c, lw_effects.c,
  lw_movement.c, lw_pathfinding.c, lw_los.c, lw_legal.c,
  lw_features.c, lw_critical.c, lw_order.c, lw_winner.c,
  lw_turn.c, lw_summon.c, lw_action.c, lw_area.c, lw_catalog.c,
  lw_effect_dispatch.c, lw_effect_store.c
bindings/python/
  leekwars_c/_engine.pyx   -- Cython binding for AI search loop
```

### v2 (`src_v2/` + `include_v2/`)
```
include_v2/
  lw_constants.h    -- All Java enums consolidated
  lw_action_stream.h -- LwActionLog with up to 8 fields per entry
  lw_state.h        -- 1:1 LwState mirror of Java State
  lw_map.h          -- 1:1 LwMap mirror of Java Map
  lw_entity.h       -- 1:1 LwEntity mirror
  lw_chip.h, lw_weapon.h, lw_attack.h, lw_effect.h, lw_los.h
  lw_pathfinding_astar.h, lw_bulb_template.h, lw_scenario.h
  lw_generator.h, lw_fight.h, lw_outcome.h
  lw_team.h, lw_order.h, lw_start_order.h, lw_actions.h
src_v2/
  77 .c files, one per Java class group (1:1 port)
  - 56 lw_effect_*.c (one per Java EffectXxx)
  - 17 areas in lw_area.c
  - lw_state.c, lw_map.c, lw_entity.c, lw_fight.c, lw_generator.c
  - lw_los.c, lw_pathfinding_astar.c, lw_bulb_template.c
  - lw_glue.c (signature bridges + binding-side stubs)
bindings/python/
  leekwars_c_v2/_engine.pyx   -- Cython binding for run_scenario
                                  + add_weapon, add_chip, add_summon_template
                                  + set_ai_callback, fire_weapon, use_chip,
                                    move_toward, cell_distance(2)
```

## Recommandation

**Garder les deux.** Elles servent des cas différents :
- **v1** : moteur de recherche d'AI (MCTS, beam search). Hot loop optimisé.
- **v2** : moteur de référence pour parity testing + entraînement RL où chaque action stream doit matcher le Java.

Le hot loop de Phase B (training) doit utiliser **v2** pour garantir que les outcomes du self-play matchent le ladder Java. Le binding de search peut utiliser **v1** pour le perf.

## Tests représentatifs

### v1 (`tests/`)
Tests unitaires C : `make test` lance `lw_*_test` qui valident les formules une par une.
Plus le **parity_gate.py** Python : 114000 cases random vs Python upstream.

### v2 (`bindings/python/test_v2_*.py`)
- `test_v2_parity.py` — 1000+ seeds 1v1 byte-identical
- `test_v2_multi.py` — 2v2/3v3/4v4 multi-team
- `test_v2_randmap.py` — random map + obstacles
- `test_v2_chip.py` — chip-by-chip parity
- `test_v2_summon.py` — Bulb summons + Bulb AI
- `test_v2_move.py` — A* movement + path
- `test_v2_fuzzer.py` — random IA stress (2000 seeds)
- `test_v2_mass.py` — sweep all 36 weapons
- `test_v2_mass_chips_all.py` — sweep all 109 chips
