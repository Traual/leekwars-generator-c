# leekzero — AlphaZero-style training on the C engine

Self-play V-network training pipeline that drives the C engine
(`leekwars_c._engine`) directly. The trained weights are intended
to deploy on `leekwars.com` via a hand-written LeekScript MLP
forward pass (the architecture is intentionally small to fit the
LeekScript 10 M mul/leek-turn budget).

```
   ┌──────────────────┐
   │ ParallelSelfPlay │  N workers  (Engine.clone + agent + extract_features)
   └────────┬─────────┘
            │ (state, label) tuples
            ▼
     ┌─────────────┐
     │ ReplayBuffer│  pre-allocated FIFO
     └──────┬──────┘
            │ batched samples
            ▼
       ┌─────────┐
       │ trainer │  AdamW, MSE(V, label), grad clip
       └────┬────┘
            │ candidate weights every cycle
            ▼
       ┌─────────┐
       │  eval   │  candidate vs frozen best
       └────┬────┘
            │ promote on win-rate >= 55 %
            ▼
        best.pt
```

## Files

| file | role |
|---|---|
| `model.py` | `MLPv1` (99 K) and `HybridV1` (125 K, CNN + MLP fusion) |
| `agent.py` | `GreedyVAgent` (MLP) and `HybridGreedyAgent` (Hybrid) |
| `scenarios.py` | `make_1v1_stage1_static`, `make_1v1_stage2_movement_obstacles` |
| `selfplay.py` | `play_one_fight`, `play_one_fight_hybrid`, replay buffers |
| `parallel.py` | `ParallelSelfPlay` (multiprocessing.Pool with per-worker model) |
| `trainer.py` | main training loop |
| `smoke_agent.py` | end-to-end smoke test for the agent |

## Curriculum stages

| stage | scenario | what it adds |
|---|---|---|
| `stage1` | fixed cells 72/144, no obstacles | bootstrap; symmetric |
| `stage2` | random spawn + 5-12 random obstacles per seed | LoS / pathfinding diversity |
| stage3 (todo) | 4v4 team | multi-entity coordination |
| stage4 (todo) | real loadouts from `leeks_builds/` | production-shaped |

## V-network architectures

| arch | params | fwd cost (b=1, CPU) | when to use |
|---|---|---|---|
| `MLPv1` | 99 K | 60 µs | stage 1, fast iteration, LeekScript port |
| `HybridV1` | 125 K | 230 µs | stage 2+, LoS / obstacle awareness |

## Bench numbers (5600X, 6c/12t)

| primitive | cost |
|---|---|
| `Engine.clone()` | 74 µs |
| `extract_v_features` | 0.27 µs |
| `MLPv1.forward` (batch=1) | 59 µs |
| `MLPv1.forward` (batch=64) | 2 µs/sample |
| Single-process self-play | 54 fights/s |
| Parallel self-play (8 workers) | **250-330 fights/s sustained** |

A 24 h run at 280 fights/s on this hardware = **~24 M fights**.

## Running

```bash
# Smoke test the agent
python bindings/python/leekzero/smoke_agent.py

# Bench parallel speedup
python bindings/python/leekzero/parallel.py

# Short test run (5 min)
python bindings/python/leekzero/trainer.py \
    --cycles 30 --fights 500 --steps 250 \
    --workers 8 --pool-refresh-every 3 \
    --eval-every 5 --eval-fights 200 \
    --buffer 200000 \
    --out-dir leekzero_runs/test

# Stage 2 + Hybrid (recommended for the 24-48h run on 5900X)
python bindings/python/leekzero/trainer.py \
    --scenario stage2 --model hybrid \
    --cycles 0 \
    --fights 500 --steps 250 \
    --workers 12 --pool-refresh-every 3 \
    --eval-every 8 --eval-fights 300 \
    --buffer 50000 --batch 256 \
    --lr 5e-4 --grad-clip 1.0 \
    --out-dir leekzero_runs/stage2_hybrid_long

# Stage 1 + MLP (faster iteration, baseline)
python bindings/python/leekzero/trainer.py \
    --cycles 0 \
    --fights 1000 --steps 400 \
    --workers 12 --pool-refresh-every 2 \
    --eval-every 10 --eval-fights 400 \
    --buffer 200000 --lr 1e-3 --grad-clip 1.0 \
    --out-dir leekzero_runs/stage1_mlp_long
```

## Hyperparameters cheat sheet

| flag | default | what to tune |
|---|---|---|
| `--workers` | 0 (single-proc) | match physical cores; on 5600X = 8, 5900X = 12 |
| `--fights` | 200 | bigger = better data per cycle, longer cycle |
| `--steps` | 200 | training steps per cycle. Aim for ~ samples added / batch |
| `--batch` | 256 | standard for ~100 K param MLP |
| `--buffer` | 200 000 | smaller = tracks current policy better but less stable |
| `--lr` | 1e-3 | classic AdamW. 2e-3 if data is noisy and you need faster catch-up |
| `--grad-clip` | 1.0 | RL stability guard rail |
| `--pool-refresh-every` | 5 | rebuild worker pool with new weights every K cycles |
| `--eval-every` | 10 | how often to test candidate vs best |
| `--eval-fights` | 100 | symmetric fights; 200+ for low-variance promotion gate |

## Known plateau / off-policy drift

Stage-1 (1v1 stationary, pistol + flame, MLPv1) plateaus around
MAE 0.70-0.73 / win-rate ~53 % on the eval gate. Two culprits:

1. **Symmetric self-play with identical V both sides** -- fights
   become near-mirror, labels are noisy.
2. **256-d entity-only features** don't see the map; LoS / obstacle
   awareness needs spatial channels.

Stage-2 + HybridV1 unlocks LoS reasoning (CNN sees the obstacle
grid) AND adds map diversity (random spawn + obstacle layouts).
Initial 20-cycle test shows MAE breaking through to 0.66 but the
eval gate stays around 50 % -- a classic off-policy regression
where the candidate fits the *buffer* (full of stale-policy data)
but doesn't translate to actually winning more fights vs the best.

Knobs to tune for the long run:

| knob | direction | reason |
|---|---|---|
| `--lr` | lower (5e-4 or 3e-4) | candidate stops over-fitting old buffer |
| `--buffer` | smaller (30-50 K) | faster turnover toward current policy |
| `--pool-refresh-every` | 2 | workers see new weights more often |
| `--eval-fights` | 300+ | reduce eval noise (100 fights = ±5% std) |
| `--eval-every` | smaller (5-8) | catch promotions earlier |

Stage-2 with these defaults should converge in ~6-12 h on the
5900X. If the eval still flat-lines, consider:

* Mixing in a fixed heuristic opponent (replay_top.py-style AI)
  during eval so the candidate gets a clear "win" signal vs a
  non-symmetric baseline.
* Smaller anneal: drop `eps_anneal_cycles` so exploration tapers
  faster once V starts mattering.

## Roadmap

- [x] Phase 0: `Engine.clone()`, `extract_v_features`
- [x] Phase 1: `MLPv1`
- [x] Phase 2: `GreedyVAgent`
- [x] Phase 3: self-play, `ReplayBuffer`
- [x] Phase 4: `trainer`, eval gate
- [x] Phase 5b: parallel self-play (4-5x speedup)
- [x] Phase 5c: HP-margin labels, grad clip, eval temperature
- [x] Stage 2: random spawn + random obstacles per seed
- [x] Spatial features (4 x 18 x 35) extracted in C
- [x] HybridV1 (CNN + entity MLP fusion) wired through agent /
       selfplay / parallel / trainer
- [ ] Long-run hyperparameter tuning to break the off-policy
       regression (see "Known plateau" above)
- [ ] Stage 3: 4v4 team
- [ ] LeekScript port: dump weights as Leek arrays + write MLP
       forward + handcrafted feature extraction in LeekScript
