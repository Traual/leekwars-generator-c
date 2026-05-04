"""Multiprocessing self-play for Leek-Zero.

Drop-in replacement for ``selfplay.collect_self_play`` that fans
fights out to a worker pool. Designed for CPU-only hosts with
many cores -- on a 5600X (6c/12t) we expect ~6-8x speedup vs the
single-process loop.

Caveat: PyTorch on CPU spawns its own thread pool by default. With
N worker processes each doing torch.matmul, those thread pools
oversubscribe the cores and the parallel speedup vanishes. The
worker initializer below sets torch threads to 1 inside each
worker; the main process can keep its default thread count for
training.

Usage:

    >>> from leekzero.parallel import ParallelSelfPlay
    >>> sp = ParallelSelfPlay(n_workers=8)
    >>> sp.set_model(model.state_dict(), eps=0.5, temperature=0.5)
    >>> states, labels, winners = sp.play_batch(n_fights=200, seed_base=...)
    >>> sp.close()

The pool is recreated on every ``set_model`` to push the new
weights into the workers. That's a few hundred ms of overhead per
cycle which is negligible against tens of seconds of fight play.
"""
from __future__ import annotations

import os
import sys
import multiprocessing as mp
from typing import Optional, Sequence

import numpy as np


# --- Worker globals (set by the initializer; never touched outside the
# worker process). Defining them at module level so worker functions
# can refer to them without juggling closures across the pickle
# boundary.
_W_AGENT = None              # leekzero.agent.GreedyVAgent
_W_SCENARIO_FN = None        # callable: (seed) -> Engine
_W_TORCH_THREADS = 1


def _worker_init(model_state_dict, weapons, chips,
                  temperature, epsilon, action_prior,
                  scenario_fn_path, torch_threads,
                  model_kind="mlp"):
    """Pool initializer: build the per-worker agent + scenario factory.

    ``scenario_fn_path`` is a "module:attribute" string instead of a
    direct callable, because some scenario functions sit on objects
    that don't pickle cleanly (engines, etc.). We import the symbol
    here in the worker.
    """
    global _W_AGENT, _W_SCENARIO_FN, _W_TORCH_THREADS

    # Make sure the worker can find the binding + leekzero package.
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    if os.path.join(here, "..") not in sys.path:
        sys.path.insert(0, os.path.join(here, ".."))
    py_upstream = "C:/Users/aurel/Desktop/leekwars_generator_python"
    if py_upstream not in sys.path:
        sys.path.insert(0, py_upstream)

    # Throttle torch thread count so 8 workers don't oversubscribe.
    import torch
    torch.set_num_threads(int(torch_threads))
    _W_TORCH_THREADS = int(torch_threads)

    from leekzero.model import MLPv1, HybridV1
    from leekzero import agent as agent_mod

    if model_kind == "hybrid":
        model = HybridV1()
    else:
        model = MLPv1()
    model.load_state_dict(model_state_dict)
    model.eval()

    if model_kind == "hybrid":
        ag = agent_mod.HybridGreedyAgent(
            model,
            weapons=list(weapons),
            chips=list(chips),
            temperature=float(temperature),
            epsilon=float(epsilon),
            rng=np.random.default_rng(os.getpid()),
        )
    else:
        ag = agent_mod.GreedyVAgent(
            model,
            weapons=list(weapons),
            chips=list(chips),
            temperature=float(temperature),
            epsilon=float(epsilon),
            rng=np.random.default_rng(os.getpid()),
        )
    if action_prior is not None:
        ag.action_prior = dict(action_prior)
    _W_AGENT = ag

    # Resolve the scenario factory.
    mod_name, _, attr = scenario_fn_path.rpartition(":")
    import importlib
    _W_SCENARIO_FN = getattr(importlib.import_module(mod_name), attr)


def _worker_play_one(seed):
    """Worker entry point (MLP / entity-only model): play one fight
    and return (states, labels, winner)."""
    from leekzero.selfplay import play_one_fight
    states, labels, winner = play_one_fight(
        _W_AGENT, scenario=_W_SCENARIO_FN, seed=int(seed))
    return states, labels, winner


def _worker_play_one_hybrid(seed):
    """Worker entry point for the Hybrid model: returns
    (entity_states, spatial_states, labels, winner)."""
    from leekzero.selfplay import play_one_fight_hybrid
    ent, spt, labels, winner = play_one_fight_hybrid(
        _W_AGENT, scenario=_W_SCENARIO_FN, seed=int(seed))
    return ent, spt, labels, winner


class ParallelSelfPlay:
    """Worker pool wrapper that produces self-play samples in
    parallel. The pool is rebuilt whenever ``set_model`` is called
    so the latest weights propagate to every worker.
    """

    def __init__(self, n_workers: int = 8,
                  scenario_fn_path: str = "leekzero.selfplay:make_1v1_pistol_flame",
                  torch_threads: int = 1,
                  model_kind: str = "mlp"):
        self.n_workers = int(n_workers)
        self.scenario_fn_path = scenario_fn_path
        self.torch_threads = int(torch_threads)
        self.model_kind = model_kind
        self._pool: Optional[mp.pool.Pool] = None
        # Args that get baked into the pool initializer:
        self._init_args: Optional[tuple] = None

    def set_model(self, model_state_dict,
                   weapons: Sequence[int],
                   chips: Sequence[int],
                   *,
                   temperature: float = 0.5,
                   epsilon: float = 0.5,
                   action_prior: Optional[dict[int, float]] = None) -> None:
        """Tear down any existing pool and spin up a fresh one with
        the given model weights and exploration settings."""
        self.close()
        # Cast state dict tensors to plain CPU tensors for safe
        # pickling.
        import torch
        cpu_state = {k: v.detach().cpu().clone() for k, v in model_state_dict.items()}
        self._init_args = (
            cpu_state,
            list(weapons),
            list(chips),
            float(temperature),
            float(epsilon),
            None if action_prior is None else dict(action_prior),
            self.scenario_fn_path,
            self.torch_threads,
            self.model_kind,
        )
        # On Windows, ``spawn`` is the default start method. We force
        # it explicitly so behavior is identical on Linux too.
        ctx = mp.get_context("spawn")
        self._pool = ctx.Pool(
            self.n_workers,
            initializer=_worker_init,
            initargs=self._init_args,
        )

    def play_batch(self, n_fights: int, seed_base: int = 0) -> tuple:
        """Run ``n_fights`` fights across the worker pool. Returns
        (states, labels, winners) for MLP, or (entity, spatial,
        labels, winners) for Hybrid.
        """
        if self._pool is None:
            raise RuntimeError("set_model() must be called before play_batch")
        seeds = [seed_base + i for i in range(n_fights)]

        if self.model_kind == "hybrid":
            ent_chunks = []
            spt_chunks = []
            lab_chunks = []
            winners: list[int] = []
            for ent, spt, labels, winner in self._pool.imap_unordered(
                    _worker_play_one_hybrid, seeds, chunksize=4):
                ent_chunks.append(ent)
                spt_chunks.append(spt)
                lab_chunks.append(labels)
                winners.append(int(winner))
            if not ent_chunks:
                return (np.zeros((0, 256), dtype=np.float32),
                        np.zeros((0, 4, 18, 35), dtype=np.float32),
                        np.zeros((0,), dtype=np.float32), [])
            return (np.concatenate(ent_chunks, axis=0),
                    np.concatenate(spt_chunks, axis=0),
                    np.concatenate(lab_chunks, axis=0),
                    winners)

        # MLP / entity-only path.
        all_states = []
        all_labels = []
        winners: list[int] = []
        for states, labels, winner in self._pool.imap_unordered(
                _worker_play_one, seeds, chunksize=4):
            all_states.append(states)
            all_labels.append(labels)
            winners.append(int(winner))
        if not all_states:
            return (np.zeros((0, 256), dtype=np.float32),
                    np.zeros((0,), dtype=np.float32), [])
        return (np.concatenate(all_states, axis=0),
                np.concatenate(all_labels, axis=0),
                winners)

    def close(self) -> None:
        if self._pool is not None:
            self._pool.close()
            self._pool.join()
            self._pool = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


# Standalone bench: how much speedup do we get over the single-
# process self-play loop?
if __name__ == "__main__":
    import time

    HERE = os.path.dirname(os.path.abspath(__file__))
    if os.path.join(HERE, "..") not in sys.path:
        sys.path.insert(0, os.path.join(HERE, ".."))
    py_upstream = "C:/Users/aurel/Desktop/leekwars_generator_python"
    if py_upstream not in sys.path:
        sys.path.insert(0, py_upstream)

    import torch
    from leekzero.model import MLPv1

    torch.manual_seed(0)
    model = MLPv1()
    state = model.state_dict()

    print("=== single-process baseline ===")
    from leekzero import agent as agent_mod
    from leekzero.selfplay import play_one_fight, WEAPON_PISTOL, CHIP_FLAME
    a = agent_mod.GreedyVAgent(
        model, weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
        temperature=0.5, epsilon=0.5)
    N = 200
    t0 = time.perf_counter()
    n_samples = 0
    for k in range(N):
        s, l, w = play_one_fight(a, seed=k)
        n_samples += s.shape[0]
    dt = time.perf_counter() - t0
    print(f"  {N} fights in {dt:.2f}s -> {N/dt:.1f} fights/s, {n_samples} samples")

    for n_workers in (4, 8, 12):
        print(f"\n=== parallel n_workers={n_workers} ===")
        sp = ParallelSelfPlay(n_workers=n_workers)
        sp.set_model(state, [WEAPON_PISTOL], [CHIP_FLAME],
                      temperature=0.5, epsilon=0.5)
        # warm-up so the bench excludes pool spin-up.
        _ = sp.play_batch(8, seed_base=10_000)
        t0 = time.perf_counter()
        s, l, ws = sp.play_batch(N, seed_base=20_000)
        dt = time.perf_counter() - t0
        print(f"  {N} fights in {dt:.2f}s -> {N/dt:.1f} fights/s, {s.shape[0]} samples")
        sp.close()
