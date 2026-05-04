"""Per-component profiler for the Leek-Zero training pipeline.

Runs N fights with the Hybrid agent on stage-2 and breaks down where
the wall time goes:

  * engine.clone()
  * engine.extract_v_features()
  * engine.extract_spatial_features()
  * model.forward()  (the NN)
  * agent.apply_action() on the live engine (atomic ops)
  * everything else (callback bookkeeping, candidate enumeration,
    selection, label backfill, ...)

Also runs an end-to-end benchmark with and without DirectML (AMD
GPU), if installed, so we can tell whether the GPU is worth wiring
in.

Usage:
    python bindings/python/leekzero/profile_pipeline.py [N]
"""
from __future__ import annotations

import os
import sys
import time
from dataclasses import dataclass, field

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

from leekzero.model import HybridV1, MLPv1, FEATURE_DIM
from leekzero.agent import (
    Action, END_TURN, MOVE_TOWARD, FIRE, CAST,
    enumerate_candidates, apply_action,
)
from leekzero.scenarios import (
    make_1v1_stage2_movement_obstacles,
    WEAPON_PISTOL, CHIP_FLAME,
)


SPATIAL_C, SPATIAL_H, SPATIAL_W = 4, 18, 35
SPATIAL_TOTAL = SPATIAL_C * SPATIAL_H * SPATIAL_W


@dataclass
class Phases:
    """Cumulative time spent in each pipeline phase (seconds)."""
    clone:      float = 0.0
    apply_clone: float = 0.0
    feat_ent:   float = 0.0
    feat_spt:   float = 0.0
    nn_forward: float = 0.0
    select:     float = 0.0
    apply_real: float = 0.0
    enum:       float = 0.0
    misc:       float = 0.0
    total:      float = 0.0
    n_decisions: int = 0
    n_candidates: int = 0
    n_fights:    int = 0


def _instrumented_score(eng, idx, candidates, model, device, spat_buf, ent_buf, ph: Phases):
    """Mirror of HybridGreedyAgent._score_candidates with per-phase
    time accounting. Mutates ``ph`` in place; returns the value
    array (numpy float32)."""
    K = len(candidates)
    illegal = [False] * K

    for i, a in enumerate(candidates):
        if a.verb == END_TURN:
            t = time.perf_counter()
            eng.extract_v_features(idx, ent_buf[i])
            ph.feat_ent += time.perf_counter() - t
            t = time.perf_counter()
            eng.extract_spatial_features(idx, spat_buf[i])
            ph.feat_spt += time.perf_counter() - t
            continue

        t = time.perf_counter()
        c = eng.clone()
        ph.clone += time.perf_counter() - t

        t = time.perf_counter()
        rc = apply_action(c, idx, a)
        ph.apply_clone += time.perf_counter() - t
        if rc <= 0:
            illegal[i] = True
            t = time.perf_counter()
            eng.extract_v_features(idx, ent_buf[i])
            ph.feat_ent += time.perf_counter() - t
            t = time.perf_counter()
            eng.extract_spatial_features(idx, spat_buf[i])
            ph.feat_spt += time.perf_counter() - t
            continue

        t = time.perf_counter()
        c.extract_v_features(idx, ent_buf[i])
        ph.feat_ent += time.perf_counter() - t
        t = time.perf_counter()
        c.extract_spatial_features(idx, spat_buf[i])
        ph.feat_spt += time.perf_counter() - t

    # Forward.
    t = time.perf_counter()
    spat_t = torch.from_numpy(spat_buf[:K]).reshape(K, SPATIAL_C, SPATIAL_H, SPATIAL_W)
    ent_t  = torch.from_numpy(ent_buf[:K])
    if device != "cpu":
        spat_t = spat_t.to(device, non_blocking=True)
        ent_t  = ent_t.to(device, non_blocking=True)
    with torch.no_grad():
        v = model(spat_t, ent_t)
    if device != "cpu":
        v = v.cpu()
    v = v.detach().numpy().astype(np.float32, copy=False)
    ph.nn_forward += time.perf_counter() - t

    for i in range(K):
        if illegal[i]:
            v[i] = -1e6
    ph.n_candidates += K
    return v


def profile_one_fight(model, device: str, seed: int, ph: Phases,
                       max_actions_per_turn: int = 32) -> int:
    """Run one fight, accumulating time in ``ph``. Returns number of
    decisions made by the agent."""
    eng = make_1v1_stage2_movement_obstacles(seed=seed)

    # Pre-allocate scratch buffers (sized for the worst-case
    # candidate list -- few dozen actions max in 1v1).
    SCRATCH = 64
    ent_buf  = np.zeros((SCRATCH, FEATURE_DIM), dtype=np.float32)
    spat_buf = np.zeros((SCRATCH, SPATIAL_TOTAL), dtype=np.float32)
    n_dec = 0

    rng = np.random.default_rng(seed + 1)

    def cb(idx: int, turn: int) -> int:
        nonlocal n_dec
        applied = 0
        while applied < max_actions_per_turn:
            t = time.perf_counter()
            cands = enumerate_candidates(eng, idx, [WEAPON_PISTOL], [CHIP_FLAME])
            ph.enum += time.perf_counter() - t
            if not cands: break
            # Truncate to scratch capacity.
            if len(cands) > SCRATCH:
                cands = cands[:SCRATCH]

            v = _instrumented_score(eng, idx, cands, model, device,
                                      spat_buf, ent_buf, ph)
            t = time.perf_counter()
            # eps-explore + temperature mirror the production agent.
            if rng.random() < 0.3:
                non_end = [i for i, a in enumerate(cands) if a.verb != END_TURN]
                best = int(rng.choice(non_end)) if non_end else 0
            else:
                scaled = v / 0.5
                scaled -= scaled.max()
                p = np.exp(scaled, dtype=np.float64); p /= p.sum()
                best = int(rng.choice(len(cands), p=p))
            ph.select += time.perf_counter() - t

            chosen = cands[best]
            n_dec += 1
            if chosen.verb == END_TURN: break

            t = time.perf_counter()
            rc = apply_action(eng, idx, chosen)
            ph.apply_real += time.perf_counter() - t
            if rc <= 0: break
            applied += 1
        return applied

    eng.set_ai_callback(cb)
    t0 = time.perf_counter()
    eng.run()
    ph.total += time.perf_counter() - t0
    ph.n_decisions += n_dec
    ph.n_fights += 1
    return n_dec


def run_one_device(device: str, n_fights: int,
                     torch_threads: int = 1) -> Phases:
    """Profile ``n_fights`` on ``device``. ``torch_threads`` is what
    a self-play worker sees -- 1 mirrors the actual training setup
    (each multiprocessing worker pins itself to 1 thread to avoid
    oversubscription)."""
    print(f"\n=== device = {device}  torch_threads = {torch_threads} ===")
    if str(device) == "cpu":
        torch.set_num_threads(torch_threads)
    model = HybridV1().to(device).eval()
    ph = Phases()
    # Warm-up.
    profile_one_fight(model, device, seed=999, ph=Phases())
    for k in range(n_fights):
        profile_one_fight(model, device, seed=k, ph=ph)
    return ph


def report(ph: Phases) -> None:
    instrumented = (ph.clone + ph.apply_clone + ph.feat_ent + ph.feat_spt
                    + ph.nn_forward + ph.select + ph.apply_real + ph.enum)
    other = max(0.0, ph.total - instrumented)
    print(f"  fights:           {ph.n_fights}")
    print(f"  decisions:        {ph.n_decisions}")
    print(f"  candidates total: {ph.n_candidates}  "
          f"(avg {ph.n_candidates/max(ph.n_decisions,1):.1f} / decision)")
    print(f"  total time:       {ph.total*1000:.1f} ms  "
          f"({ph.n_fights/max(ph.total,1e-9):.1f} fights/s)")
    print(f"  per fight:        {1000*ph.total/max(ph.n_fights,1):.1f} ms")
    print()
    print("  phase                           total ms     %    per-decision us")
    rows = [
        ("clone()",                         ph.clone),
        ("apply_action on clone",           ph.apply_clone),
        ("extract_v_features (entity)",     ph.feat_ent),
        ("extract_spatial_features",        ph.feat_spt),
        ("model.forward (NN)",              ph.nn_forward),
        ("epsilon/softmax select",          ph.select),
        ("apply_action on live engine",     ph.apply_real),
        ("enumerate_candidates",            ph.enum),
        ("(other: engine internals + cb)",  other),
    ]
    for name, t in rows:
        pct = 100 * t / max(ph.total, 1e-9)
        per_dec = 1e6 * t / max(ph.n_decisions, 1)
        print(f"  {name:<30s}    {t*1000:>8.1f}   {pct:>4.1f}    {per_dec:>8.1f}")


def has_directml() -> bool:
    try:
        import torch_directml  # noqa: F401
        return True
    except Exception:
        return False


def main():
    n_fights = int(sys.argv[1]) if len(sys.argv) > 1 else 20

    print(f"Profiling Hybrid + stage 2 over {n_fights} fights / device.")

    # Two CPU runs: one mimicking a parallel worker (1 torch thread,
    # the actual training mode) and one with all cores so we see
    # whether pytorch's intra-op parallelism helps single-process
    # serial benches. The training pipeline uses N=1 because we
    # spawn 8-12 worker processes.
    ph_cpu1 = run_one_device("cpu", n_fights, torch_threads=1)
    print()
    report(ph_cpu1)

    n_threads_max = max(1, os.cpu_count() or 1)
    ph_cpu_all = run_one_device("cpu", n_fights, torch_threads=n_threads_max)
    print()
    report(ph_cpu_all)

    if has_directml():
        import torch_directml
        dml = torch_directml.device(0)
        print(f"\n[directml] device available: {dml}")
        try:
            ph_dml = run_one_device(dml, n_fights)
            print()
            report(ph_dml)
            speedup = ph_cpu1.total / max(ph_dml.total, 1e-9)
            print(f"\n  CPU(1 thread) vs DirectML speedup: {speedup:.2f}x "
                  f"({'DirectML faster' if speedup > 1 else 'CPU faster'})")
        except Exception as exc:
            print(f"\n[directml] FAILED: {exc}")
    else:
        print("\n[directml] torch_directml not installed -- skipping GPU bench.")
        print("    pip install torch-directml")
        print("  to add the AMD GPU backend (optional).")


if __name__ == "__main__":
    main()
