# AlphaZero for Leek Wars — training plan

This plan turns the C engine into an AlphaZero-style self-play loop that
produces the strongest possible Leek Wars AI given (a) infinite compute
budget but (b) zero engineering shortcuts.

The engine is the **environment**. This document is everything else: the
neural network, the search, the training pipeline, the evaluation gates,
and the order of operations.

---

## 0. Current state (what's already done)

### Engine parity vs upstream Python (= line-by-line port of Java)

| Surface | Coverage | Result |
|---|---|---|
| 1v1 weapons + chips + summons + death scenarios + fuzzer | 6 030 fights | **100 % byte-identical** |
| 4v4 chip+weapon (TYPE_TEAM, no move) | 20 seeds | **20 / 20** |
| 6v6 chip+weapon (TYPE_TEAM, no move) | 20 seeds | **20 / 20** |
| 2-player BR (TYPE_BATTLE_ROYALE) | 3 seeds | **3 / 3** |

### Known engine gaps (real, must be fixed before training)

| # | Bug | Impact | Severity |
|---|---|---|---|
| 1 | `move_toward` tie-break differs from Python upstream when two cells are equidistant | multi-entity fights with movement diverge ~1 % | medium |
| 2 | Effect-cleanup after `PLAYER_DEAD` in BR mode (≥4 players) emits extra `STACK_EFFECT` increments | BR ≥ 4 players diverge after the first kill | high |
| 3 | Direct Java cross-check shows different start-order convention (LEEK_TURN argument is leek-id in Java, fight-id in Python upstream + v2) | cosmetic if mapped; *but* the actual fight outcome can diverge by 1-2 turns under heavy combat | medium |

These don't block AlphaZero training as long as we (a) train and deploy
on the same engine and (b) fix bug #2 before any BR self-play.

### Engine speed

```
Per fight (basic AI, no I/O):
  Python upstream:    386 fights/sec
  C engine:        55 689 fights/sec        (144x)
RNG:                 39 M draws/sec          (21x)
```

---

## 1. Architecture overview

```
                ┌───────────────────────────────────┐
                │            REPLAY BUFFER          │
                │   (state, π, z) tuples on disk    │
                └────────┬─────────────────┬────────┘
                         │                 │
                  sample │                 │ append
                         ▼                 │
               ┌──────────────┐    ┌───────┴───────┐
               │   TRAINER    │    │  SELF-PLAY    │
               │ (PyTorch /   │    │   workers     │
               │  GPU node)   │    │ (CPU pool)    │
               └──────┬───────┘    └───────┬───────┘
                      │ checkpoint         │ load
                      ▼                    │
               ┌──────────────┐            │
               │  EVAL GATE   │◀───────────┘
               │ new vs best  │
               └──────┬───────┘
                      │ promote on win-rate ≥ 55 %
                      ▼
               ┌──────────────┐
               │  BEST MODEL  │
               └──────────────┘
```

---

## 2. Game representation

### State tensor (input to the network)

A Leek Wars fight is a turn-based 2D game on an 18×18 diamond grid with
up to ~40 entities (BR), each carrying ≤ 16 weapons + ≤ 32 chips +
multiple stat dimensions. The cleanest fixed-size encoding:

**Spatial channels (18 × 18 × C):**
| ch | what |
|---|---|
| 0 | terrain walkability (1 if walkable) |
| 1 | obstacle type (one-hot or category id) |
| 2-3 | active leek mask (us / them) |
| 4-7 | per-entity HP fraction, TP fraction, MP fraction, alive |
| 8-15 | active effect channels (poison, shield, shackle, vulnerability, etc.) |
| 16+ | summon mask, line-of-sight from active leek, distance map to nearest enemy |

**Per-entity feature vector (E × F):** stats + level + cooldowns + buff
counts + chip availability mask. Pad to E_MAX = 16 entities. Concatenate
with attention pooling.

**Loadout / scenario tensor:** weapons available, chips available,
remaining turn budget, fight type, team membership.

### Action space

Up to ~10 000 raw actions per turn (move + chip + weapon × cells), but
~95 % are illegal in any given state. Use a **masked policy head** over
a structured action vocabulary:

- `END_TURN` (1)
- `MOVE_TO(cell)` (612)
- `USE_WEAPON(target_cell)` (612)
- `USE_CHIP(chip_id, target_cell)` (~109 × 612 in principle, but most
  entities only have ≤ 16 chips → ≤ 16 × 612 ≈ 10 000)

→ Total ≤ 11 225 logits, mask down to legal subset before softmax.

### Reward

End-of-fight terminal reward only:
- TYPE_TEAM / 1v1: `+1` win, `-1` loss, `0` draw, scaled by margin
  (HP delta) for shaping.
- BR: `+1` first place, `-1` last, linear interpolation in between.

---

## 3. Network

**MuZero-style separation** is overkill — Leek Wars has a perfect-info
deterministic transition function (the engine), so plain AlphaZero with
the engine as ground truth is sufficient.

```
                       ┌─── policy head (masked logits) ──→ π
input  ─→  trunk  ──→ ┤
                       └─── value head (tanh)            ──→ v
```

**Trunk: ResNet-style on the spatial input** (18×18 is small enough
that conv layers dominate cheap):

- 6× residual blocks, 128 channels, 3×3 conv + BN + ReLU
- + per-entity attention block (cross-attend spatial features over the
  per-entity tensor) — 2 heads × 64 dim
- → flatten + MLP (2 × 256) → policy + value heads

Roughly 2-5 M parameters. Trains comfortably on a single GPU.

---

## 4. Search: MCTS with PUCT

Standard AlphaZero MCTS:

```
N(s,a)  = visit count
W(s,a)  = total value
Q(s,a)  = W / N
U(s,a)  = c_puct · π(s,a) · √ΣN / (1 + N(s,a))
select a = argmax (Q + U)
```

**Per-turn budget**: 400 simulations. With our 17 µs/fight engine, a
batch of 64 simulations can be evaluated in ~1 ms even on CPU. The
bottleneck is the network forward pass — batch them.

**Tree state lifecycle**: clone the engine state at each node. The C
engine's pool allocator + memcpy clone (67 µs) makes this cheap. Cap
tree size at 4 096 nodes per move; recycle.

**Dirichlet noise** at root (α = 0.3 for our action vocabulary, ε = 0.25)
to drive exploration during self-play.

---

## 5. Self-play

A self-play worker:
1. Pulls the current best model from disk (mmap'd weights).
2. Generates a random scenario (sample loadouts from a curriculum, see §7).
3. Runs both sides with MCTS + the model, storing `(state, π, z)`
   triples for every move.
4. On terminal, backfills `z` and pushes the trajectory into the replay
   buffer.

**Throughput**: with 400 sims/move, ~30 moves/fight, network bottleneck
≈ 50 ms/move on GPU batch 256, a single self-play worker produces
~1 fight every 2 s. **128 workers → ~250 K fights / day on a single
8-GPU box.**

For comparison: AlphaZero Go used 5 000 first-gen TPUs to generate
44 M games over 40 days. Leek Wars is much smaller; a single 8-GPU
node should give superhuman play in 2-4 weeks.

---

## 6. Training

- Optimizer: AdamW, lr 1e-3 cosine-decayed, weight decay 1e-4.
- Loss: cross-entropy(π, π_target) + MSE(v, z) + 1e-4 · ‖θ‖².
- Batch 1024, 1 step per ~10 self-play moves added to the buffer.
- Replay buffer: 1 M most recent positions (FIFO).
- Checkpoint every 1 000 steps; eval every 5 000 steps.

---

## 7. Curriculum

Don't dump the full game in on day 1. Stage:

1. **1v1 stationary** (no move): both entities fixed, only chip + weapon.
   Smallest action space. ~100 K fights to converge.
2. **1v1 with movement**: fix bug #1 first.
3. **2v2 / 3v3 / 4v4 TEAM** (after #1 fixes).
4. **Battle royale 4-player** (after fixing bug #2).
5. **BR 6 / 8 / 10 player**.
6. **Real loadouts** sampled from the actual leeks_builds catalog
   (Astheopyrosphamidum, Krapule, etc. JSONs sitting next to the repo).

Each stage feeds into the next via warm-start from the previous best
model — initialization is much better than from scratch.

---

## 8. Evaluation gates

Before promoting a candidate to "best":
- 1 000 fights vs current best across the curriculum stages it's
  certified for.
- Promote on win-rate ≥ 55 % AND no regression on lower stages.
- Periodic round-robin tournament of last 10 best models to detect
  cycling / mode collapse.

External baselines (after we have a working pipeline):
- Top public AIs from `leeks_builds/` JSONs (Krapule, Mageas, etc.)
- Optionally: connect to the Leek Wars test arena.

---

## 9. Concrete next steps (in execution order)

### Week 0 — engine cleanup
1. **Fix bug #2** (BR effect cleanup after PLAYER_DEAD). Probably one
   missing `lw_entity_remove_launched_effect` call in the death path.
2. **Fix bug #1** (move_toward tie-break). Compare the C neighbor
   iteration order against `Pathfinding.getMinUsefulOrder` in Java.
3. **Re-run all parity tests** to confirm 4v4 / 6v6 / 10-BR all
   100 % byte-identical even with movement.

### Week 1 — Python AI substrate
4. **State encoder**: `state_to_tensor(eng) → np.ndarray` (spatial +
   per-entity + scenario), zero-copy where possible (numpy view on the
   C buffers).
5. **Action encoder/decoder**: legal-action enumerator that returns
   `(mask: BoolTensor, ids: List[Action])` for the masked policy head.
6. **Engine clone primitive**: expose `eng.clone() → Engine` returning
   a deep-copy that shares no state with the parent (for MCTS rollouts).
7. **Smoke MCTS** with a uniform prior + zero value to validate that
   the Python overhead per move is acceptable (<10 ms with batch 64).

### Week 2 — minimal AlphaZero
8. **Network**: 2-block ResNet, write training loop + replay buffer.
9. **Self-play worker** (single process, no batching) on stage 1 (1v1
   stationary). Verify the network learns to chain weapon use rather
   than cast wasted chips.
10. **Eval gate** vs random baseline; should hit 100 % win rate within
    a few thousand games.

### Week 3-4 — scale
11. Multi-process self-play workers + GPU batching server.
12. Walk the curriculum stages 2 → 5.
13. First match vs human-built AIs from `leeks_builds/`.

### Month 2+ — push past human
14. Larger network (6+ residual blocks).
15. Real loadout sampling.
16. Population-based training: keep a diverse pool of 20-50 best models;
    sample opponents from the pool to break cycles.
17. Eventually: open-loop search at evaluation time (more sims per move
    → measurably stronger play, no extra training cost).

---

## 10. Risks / things that can derail this

- **Engine drift from production Java.** We're 100 % parity vs the
  upstream Python port, but the Python port itself may have small drift
  vs the official Java engine (start-order convention, BR effect cleanup,
  etc.). When we deploy the trained AI on Leek Wars production, there
  may be 1-2 % extra losses from rule mismatches. Mitigation: every few
  weeks, sample fights from production logs and re-validate that v2 still
  matches Java behavior on real loadouts.
- **Action-mask explosion.** USE_CHIP × 109 chips × 612 cells × 32 chip
  slots is gigantic if naively enumerated every step. The masked-logits
  trick handles inference, but the *enumeration* needs to be fast (in
  C). Add `eng.legal_actions(idx) → List[ActionEncoding]` to the binding.
- **MCTS clone cost dominates.** 67 µs/clone × 400 sims = 27 ms/move
  just for engine state copies. If this becomes the bottleneck, switch
  to incremental state diffs (apply / undo) instead of full clones.
- **Reward sparsity in BR.** A 10-player BR has a single winner; 9 of
  10 trajectories see only a loss signal. Use rank-based dense reward
  (place 1 → +1, place 10 → −1, linear) instead of pure win/loss.

---

## 11. What "ultimate Leek Wars AI" looks like

After 4-8 weeks of self-play on this pipeline, the realistic expectation:

- Beats every public AI in `leeks_builds/` with > 90 % win rate.
- Discovers non-obvious strategies (early-game positioning, summon timing,
  shackle chains, kite-and-poison loops) that human-written AIs miss.
- Generalizes across loadouts (no need to retrain per build).
- Run-cost at deploy: ~50 ms / move on a single GPU, well within any
  realistic Leek Wars timer.

If we want to go further: MuZero-style learned model would let the AI
plan over imagined trajectories without re-clone, cutting per-move
compute by ~5×. Worth doing only after the basic pipeline is proven.
