"""Training loop for Leek-Zero V-network.

Alternates self-play data generation and supervised learning of
V(state) -> eventual outcome. Single-process to keep things simple
and debuggable; multiprocessing self-play is a v2 enhancement.

Loop per cycle:

  1. Run K self-play fights with the current agent. Each fight
     produces ~30 (state, label) samples; push them into the replay
     buffer.
  2. Sample mini-batches from the buffer and run M training steps.
     Loss = MSE(V(state), label).
  3. Every E cycles, eval the new V vs the previous best by playing
     symmetric matchups. Promote if win-rate >= 55 %.

Periodic checkpoints land under ``leekzero_runs/<run-id>/``:
  best.pt          : last promoted weights (warm-start path).
  candidate.pt     : weights of the model currently being trained.
  log.jsonl        : one record per cycle (fights, win-rate, loss).
  meta.json        : hyperparameters of the run.

Defaults are tuned for stage-1 (1v1 pistol+flame on the C engine):
  cycles            : unbounded -- stop with Ctrl-C
  fights_per_cycle  : 200      ~3-4 s
  train_steps       : 200      ~1-2 s
  batch_size        : 256
  buffer_capacity   : 200_000
  eval_every        : 10
  eval_fights       : 100
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Optional

import numpy as np
import torch
import torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))                  # ``leekwars_c``
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

from leekzero.model import MLPv1, FEATURE_DIM
from leekzero import agent as agent_mod
from leekzero.selfplay import (
    ReplayBuffer, make_1v1_pistol_flame, play_one_fight,
    WEAPON_PISTOL, CHIP_FLAME,
)
from leekzero.scenarios import (
    make_1v1_stage1_static, make_1v1_stage2_movement_obstacles,
)
from leekzero.parallel import ParallelSelfPlay


# Scenario registry: --scenario <name> picks one of these.
SCENARIOS = {
    "stage1": ("leekzero.scenarios:make_1v1_stage1_static",
                make_1v1_stage1_static),
    "stage2": ("leekzero.scenarios:make_1v1_stage2_movement_obstacles",
                make_1v1_stage2_movement_obstacles),
    # legacy alias for the original selfplay scenario
    "legacy_1v1": ("leekzero.selfplay:make_1v1_pistol_flame",
                    make_1v1_pistol_flame),
}


@dataclass
class Config:
    """Hyper-parameters for the training run. Saved alongside the
    checkpoint so we can reproduce a run after the fact."""
    scenario: str = "stage1"           # key into SCENARIOS dict
    cycles: int = 0                    # 0 = run forever
    fights_per_cycle: int = 200
    train_steps_per_cycle: int = 200
    batch_size: int = 256
    buffer_capacity: int = 200_000
    eval_every: int = 10
    eval_fights: int = 100
    eval_promote_threshold: float = 0.55
    learning_rate: float = 1e-3
    weight_decay: float = 1e-4
    # Exploration schedule: (eps_start, eps_end, anneal_cycles)
    eps_start: float = 0.5
    eps_end: float = 0.05
    eps_anneal_cycles: int = 50
    temperature: float = 0.5
    seed_base: int = 1
    out_dir: str = "leekzero_runs/run0"
    # Multiprocessing: 0 = single-process, >=1 = number of workers
    n_workers: int = 0
    # How many cycles between pool refreshes. Pool spin-up costs
    # ~2-3 s on Windows; refreshing every cycle wastes that on
    # shorter cycles. The workers play with stale weights between
    # refreshes, but with eps_explore in [0.05, 0.5] the staleness
    # has small effect on data quality.
    pool_refresh_every: int = 5
    # Stability: clip gradient L2-norm to this value (0 = no clip)
    grad_clip: float = 1.0


def seed_all(seed: int) -> None:
    np.random.seed(seed)
    torch.manual_seed(seed)


def load_or_init_model(path: Path) -> MLPv1:
    m = MLPv1()
    if path.is_file():
        m.load_state_dict(torch.load(path, map_location="cpu", weights_only=True))
        print(f"  warm-start loaded from {path}")
    return m


def save_model(model: nn.Module, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(model.state_dict(), path)


def collect_self_play(agent: agent_mod.GreedyVAgent,
                       buffer: ReplayBuffer,
                       n_fights: int,
                       seed_base: int,
                       scenario_fn=None) -> dict:
    """Single-process self-play: run ``n_fights`` fights, push
    samples into the buffer, and return summary stats."""
    t0 = time.perf_counter()
    n_samples = 0
    winners: dict[int, int] = {}
    for k in range(n_fights):
        if scenario_fn is None:
            s, l, w = play_one_fight(agent, seed=seed_base + k)
        else:
            s, l, w = play_one_fight(agent, scenario=scenario_fn,
                                       seed=seed_base + k)
        buffer.push(s, l)
        n_samples += s.shape[0]
        winners[w] = winners.get(w, 0) + 1
    return {
        "fights":  n_fights,
        "samples": n_samples,
        "winners": winners,
        "fights_per_sec": n_fights / max(time.perf_counter() - t0, 1e-9),
    }


def collect_self_play_parallel(sp: ParallelSelfPlay,
                                 buffer: ReplayBuffer,
                                 n_fights: int,
                                 seed_base: int) -> dict:
    """Multi-process self-play via the worker pool. Same return
    shape as ``collect_self_play``."""
    t0 = time.perf_counter()
    states, labels, winners = sp.play_batch(n_fights, seed_base=seed_base)
    buffer.push(states, labels)
    win_counts: dict[int, int] = {}
    for w in winners:
        win_counts[w] = win_counts.get(w, 0) + 1
    return {
        "fights":  n_fights,
        "samples": states.shape[0],
        "winners": win_counts,
        "fights_per_sec": n_fights / max(time.perf_counter() - t0, 1e-9),
    }


def train_steps(model: MLPv1,
                 optimizer: torch.optim.Optimizer,
                 buffer: ReplayBuffer,
                 n_steps: int,
                 batch_size: int,
                 grad_clip: float = 0.0) -> dict:
    """Run ``n_steps`` SGD updates on samples drawn from ``buffer``.
    Returns mean loss + value-prediction MAE.

    With ``grad_clip > 0`` we clip the global L2 grad norm before
    stepping (standard RL stability guard rail; default 1.0).
    """
    if len(buffer) < batch_size:
        return {"steps": 0, "loss": float("nan"), "mae": float("nan")}
    model.train()
    losses = []
    maes = []
    t0 = time.perf_counter()
    for _ in range(n_steps):
        s, l = buffer.sample(batch_size)
        x = torch.from_numpy(s)
        y = torch.from_numpy(l)
        v = model(x)
        loss = torch.nn.functional.mse_loss(v, y)
        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        if grad_clip > 0.0:
            torch.nn.utils.clip_grad_norm_(model.parameters(), grad_clip)
        optimizer.step()
        losses.append(float(loss.item()))
        maes.append(float((v.detach() - y).abs().mean().item()))
    model.eval()
    return {
        "steps":     n_steps,
        "loss":      float(np.mean(losses)),
        "mae":       float(np.mean(maes)),
        "secs":      time.perf_counter() - t0,
    }


def eval_match(candidate: MLPv1, opponent: MLPv1, n_fights: int,
                seed_base: int = 1_000_000,
                eval_temperature: float = 0.1,
                scenario_fn=None) -> dict:
    """Play ``n_fights`` symmetric 1v1s: candidate as team 1, opponent
    as team 2 for the first half, then swap. Returns win-rate of
    candidate.

    ``eval_temperature``: small softmax temperature (default 0.1) on
    both sides. Pure argmax (T=0) makes both agents deterministic
    and they tie-break identically, so even genuinely different V
    networks play mirror fights -- every match draws and the eval
    reports 50%. A small temperature lets the better V's edge come
    through statistically without flooding decisions with noise.
    """
    half = n_fights // 2
    cand_wins = 0
    opp_wins  = 0
    draws     = 0
    # We need a way to assign different agents to different teams.
    # For now we just have the candidate play one side and the
    # opponent the other by hot-swapping the model on the agent
    # before each callback. That requires running through the
    # cb-driven flow without hot-swapping mid-fight. Easy hack: build
    # one agent that picks model based on entity team.
    # First half: candidate is team 1.
    sc = scenario_fn if scenario_fn is not None else make_1v1_pistol_flame
    for k in range(n_fights):
        is_cand_team1 = (k < half)
        eng = sc(seed=seed_base + k)
        ag_cand = agent_mod.GreedyVAgent(
            candidate, weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
            temperature=eval_temperature, epsilon=0.0,
            rng=np.random.default_rng(seed_base + k))
        ag_opp = agent_mod.GreedyVAgent(
            opponent, weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
            temperature=eval_temperature, epsilon=0.0,
            rng=np.random.default_rng(seed_base + k + 1_000_000_000))
        agent_mod._callback_engine = eng
        # Per-callback: pick the agent matching the entity's team.
        def cb(idx: int, turn: int) -> int:
            t = eng.entity_team(idx)
            # team 0 = team 1 (TA, first added), team 1 = team 2 (TB)
            if (t == 0 and is_cand_team1) or (t == 1 and not is_cand_team1):
                return ag_cand.play_turn(eng, idx, turn)
            return ag_opp.play_turn(eng, idx, turn)
        eng.set_ai_callback(cb)
        eng.run()
        w = int(eng.winner)
        if w == -1:
            draws += 1
        elif (w == 0 and is_cand_team1) or (w == 1 and not is_cand_team1):
            cand_wins += 1
        else:
            opp_wins += 1
    return {
        "fights":     n_fights,
        "cand_wins":  cand_wins,
        "opp_wins":   opp_wins,
        "draws":      draws,
        "win_rate":   cand_wins / max(n_fights, 1),
    }


def epsilon_schedule(cycle: int, cfg: Config) -> float:
    if cycle >= cfg.eps_anneal_cycles:
        return cfg.eps_end
    frac = cycle / max(cfg.eps_anneal_cycles, 1)
    return cfg.eps_start + (cfg.eps_end - cfg.eps_start) * frac


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out-dir",  default="leekzero_runs/run0")
    p.add_argument("--cycles",   type=int, default=0,
                    help="0 = run forever; stop with Ctrl-C")
    p.add_argument("--fights",   type=int, default=200,
                    help="self-play fights per cycle")
    p.add_argument("--steps",    type=int, default=200,
                    help="train steps per cycle")
    p.add_argument("--batch",    type=int, default=256)
    p.add_argument("--seed",     type=int, default=1)
    p.add_argument("--eval-every",  type=int, default=10)
    p.add_argument("--eval-fights", type=int, default=100)
    p.add_argument("--lr",       type=float, default=1e-3)
    p.add_argument("--workers",  type=int, default=0,
                    help="0 = single-process; >0 = parallel self-play "
                         "with this many workers")
    p.add_argument("--pool-refresh-every", type=int, default=5,
                    help="rebuild worker pool with new weights every N "
                         "cycles (only with --workers>0)")
    p.add_argument("--grad-clip", type=float, default=1.0)
    p.add_argument("--buffer",    type=int, default=200_000,
                    help="replay buffer capacity in samples")
    p.add_argument("--scenario",  default="stage1",
                    choices=list(SCENARIOS.keys()),
                    help="curriculum stage: stage1 (fixed map) or "
                         "stage2 (random spawn + obstacles)")
    args = p.parse_args()

    cfg = Config(
        cycles=args.cycles, fights_per_cycle=args.fights,
        train_steps_per_cycle=args.steps, batch_size=args.batch,
        eval_every=args.eval_every, eval_fights=args.eval_fights,
        learning_rate=args.lr, seed_base=args.seed, out_dir=args.out_dir,
        n_workers=args.workers, grad_clip=args.grad_clip,
        buffer_capacity=args.buffer,
        pool_refresh_every=args.pool_refresh_every,
        scenario=args.scenario,
    )
    scenario_path, scenario_fn = SCENARIOS[cfg.scenario]
    print(f"[trainer] scenario = {cfg.scenario} -> {scenario_path}")
    out = Path(cfg.out_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / "meta.json").write_text(json.dumps(asdict(cfg), indent=2))
    log_path = out / "log.jsonl"
    best_path = out / "best.pt"
    cand_path = out / "candidate.pt"

    seed_all(cfg.seed_base)

    # Best (held) and candidate (training) models.
    best_model = load_or_init_model(best_path)
    cand_model = MLPv1()
    cand_model.load_state_dict(best_model.state_dict())

    optimizer = torch.optim.AdamW(cand_model.parameters(),
                                    lr=cfg.learning_rate,
                                    weight_decay=cfg.weight_decay)

    buffer = ReplayBuffer(capacity=cfg.buffer_capacity)

    # Optional parallel self-play. When n_workers>0 we keep a pool
    # alive across cycles and just refresh its weights via
    # set_model() at the top of each cycle.
    parallel_sp: Optional[ParallelSelfPlay] = None
    if cfg.n_workers > 0:
        parallel_sp = ParallelSelfPlay(
            n_workers=cfg.n_workers,
            scenario_fn_path=scenario_path)
        print(f"[trainer] parallel self-play with {cfg.n_workers} workers")

    cycle = 0
    while True:
        eps = epsilon_schedule(cycle, cfg)

        if parallel_sp is not None:
            # Refresh the worker pool's weights every N cycles only.
            # Spin-up takes 2-3 s on Windows; doing it every cycle
            # eats most of the parallel speedup on short cycles.
            # In-between refreshes, workers play with slightly stale
            # weights -- ok-ish since eps-exploration dominates the
            # decision distribution anyway.
            if cycle == 0 or cycle % cfg.pool_refresh_every == 0:
                parallel_sp.set_model(
                    cand_model.state_dict(),
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
                    temperature=cfg.temperature, epsilon=eps)
            sp_stats = collect_self_play_parallel(
                parallel_sp, buffer, cfg.fights_per_cycle,
                seed_base=cfg.seed_base + cycle * cfg.fights_per_cycle)
        else:
            agent = agent_mod.GreedyVAgent(
                cand_model, weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
                temperature=cfg.temperature, epsilon=eps)
            sp_stats = collect_self_play(
                agent, buffer, cfg.fights_per_cycle,
                seed_base=cfg.seed_base + cycle * cfg.fights_per_cycle,
                scenario_fn=scenario_fn)

        tr = train_steps(
            cand_model, optimizer, buffer,
            cfg.train_steps_per_cycle, cfg.batch_size,
            grad_clip=cfg.grad_clip)

        record = {
            "cycle": cycle,
            "eps": eps,
            "self_play": sp_stats,
            "train": tr,
            "buffer_size": len(buffer),
        }

        if cfg.eval_every > 0 and cycle > 0 and cycle % cfg.eval_every == 0:
            ev = eval_match(cand_model, best_model, cfg.eval_fights,
                              scenario_fn=scenario_fn)
            record["eval"] = ev
            if ev["win_rate"] >= cfg.eval_promote_threshold:
                save_model(cand_model, best_path)
                best_model = MLPv1()
                best_model.load_state_dict(cand_model.state_dict())
                record["promoted"] = True
            else:
                record["promoted"] = False

        # Always keep the latest candidate around so a crash doesn't
        # cost the whole cycle's progress.
        save_model(cand_model, cand_path)

        with log_path.open("a") as f:
            f.write(json.dumps(record) + "\n")

        # Pretty console line
        line = (f"cycle {cycle:>4} | eps={eps:.2f} | "
                f"sp={sp_stats['fights']} fights@{sp_stats['fights_per_sec']:.1f}/s | "
                f"buf={len(buffer)} | "
                f"loss={tr['loss']:.4f} mae={tr['mae']:.3f}")
        if "eval" in record:
            ev = record["eval"]
            line += f" | EVAL win={ev['win_rate']*100:.1f}% "
            line += "(PROMOTED)" if record.get("promoted") else "(no)"
        print(line, flush=True)

        cycle += 1
        if cfg.cycles > 0 and cycle >= cfg.cycles:
            break

    if parallel_sp is not None:
        parallel_sp.close()


if __name__ == "__main__":
    main()
