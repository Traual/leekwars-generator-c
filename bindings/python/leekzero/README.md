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
| `model.py` | `MLPv1` (256 → 256 → 128 → 1, 99 K params) |
| `agent.py` | `GreedyVAgent` (depth-1 V-guided beam, with eps + temperature) |
| `selfplay.py` | scenario builders, `play_one_fight`, `ReplayBuffer` |
| `parallel.py` | `ParallelSelfPlay` (multiprocessing.Pool with per-worker model) |
| `trainer.py` | main training loop |
| `smoke_agent.py` | end-to-end smoke test for the agent |

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

# Long run (24-48 h, recommended for the 5900X)
python bindings/python/leekzero/trainer.py \
    --cycles 0 \
    --fights 1000 --steps 400 \
    --workers 12 --pool-refresh-every 2 \
    --eval-every 10 --eval-fights 400 \
    --buffer 200000 --lr 1e-3 --grad-clip 1.0 \
    --out-dir leekzero_runs/long
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

## Known plateau

Stage-1 (1v1 stationary, pistol + flame) plateaus around MAE 0.70-
0.73 / win-rate ~53 % on the eval gate. Two culprits:

1. **Symmetric self-play with identical V both sides** -- fights
   become near-mirror, labels are noisy.
2. **256-d feature buffer** doesn't include cooldown timers,
   time-since-last-fire, recent damage taken, etc. The MLP can
   only learn what the features expose.

Stage-2 escapes by adding movement (more state variety) and
swapping the opponent for a fixed heuristic mid-curriculum so the
candidate sees decisive wins/losses.

## Roadmap

- [x] Phase 0: `Engine.clone()`, `extract_v_features`
- [x] Phase 1: `MLPv1`
- [x] Phase 2: `GreedyVAgent`
- [x] Phase 3: self-play, `ReplayBuffer`
- [x] Phase 4: `trainer`, eval gate
- [x] Phase 5b: parallel self-play (4-5x speedup)
- [x] Phase 5c: HP-margin labels, grad clip, eval temperature
- [ ] Stage 2: 1v1 with movement (need richer feature set)
- [ ] Stage 3: 4v4 team
- [ ] LeekScript port: dump weights as Leek arrays + write MLP
      forward + handcrafted feature extraction in LeekScript
