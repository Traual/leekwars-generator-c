"""Self-play and replay buffer for Leek-Zero.

Single-process initially -- we can swap to ``multiprocessing.Pool``
later without changing the public surface here. Each call to
``play_one_fight`` runs one full fight with the agent on both sides
and returns a list of ``(state_features, label)`` tuples, where
``label`` is the eventual outcome from each captured state's team
perspective (``+1`` win, ``-1`` loss, ``0`` draw).

Replay buffer is an in-memory ring with two pre-allocated numpy
arrays (states, labels). Bounded size so the trainer always sees a
recent window of self-play data.
"""
from __future__ import annotations

import os
import sys
from typing import List, Optional, Sequence, Tuple

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))                  # ``leekwars_c``
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

import leekwars_c._engine as _v2

from leekzero.model import FEATURE_DIM
from leekzero import agent as agent_mod


# ============================================================ scenarios ==

# A scenario builder returns an engine ready to run() and the
# (active-)leeks-per-team count + total entity count, so the data
# pipeline can size its scratch arrays. Scenarios are
# deterministic given (seed); the same (scenario, seed) replays the
# same fight if the agent is also deterministic (temperature=0,
# epsilon=0).

# Lightweight 1v1 used by the smoke test and stage-1 self-play.

import test_v2_summon as base   # WEAPONS / CHIPS / register helpers

WEAPON_PISTOL = 37
CHIP_FLAME    = 5


def make_1v1_pistol_flame(seed: int = 1, *,
                            life: int = 800, max_turns: int = 30) -> _v2.Engine:
    """Build a deterministic 1v1 fight: two leeks at fixed cells with
    pistol + flame chip. The same setup the smoke test uses but with
    knobs for the stage-1 warm-up.

    Defaults are tuned so even a near-random agent produces decisive
    fights (winner != -1) most of the time:

      * life=800: pistol+flame deals ~30-80 raw per turn under random
        exploration -> ~10-15 turns to KO. With max_turns=30 we have
        a comfortable margin.
      * max_turns=30: matches the engine's BR / team default.

    Override at the production scenario stage (stage 4 = real
    loadouts) to use the actual full-HP profile.
    """
    eng = _v2.Engine()
    raw = base.WEAPONS[WEAPON_PISTOL]
    eng.add_weapon(item_id=WEAPON_PISTOL, name=raw["name"], cost=int(raw["cost"]),
                    min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
                    launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
                    los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
                    effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                              int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                             for e in raw["effects"]],
                    passive_effects=[])
    base.register_all_chips_v2(eng)
    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
    eng.add_entity(team=0, fid=1, name="A", level=100, life=life, tp=14, mp=6,
                    strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                    science=0, magic=0, cores=10, ram=10, farmer=1, team_id=1,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], cell=72)
    eng.add_entity(team=1, fid=2, name="B", level=100, life=life, tp=14, mp=6,
                    strength=200, agility=0, frequency=100, wisdom=0, resistance=0,
                    science=0, magic=0, cores=10, ram=10, farmer=2, team_id=2,
                    weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME], cell=144)
    eng.set_seed(seed); eng.set_max_turns(max_turns); eng.set_type(1)
    eng.set_custom_map(obstacles={}, team1=[72], team2=[144], map_id=1)
    return eng


# ============================================================ self-play ==


def play_one_fight(agent: agent_mod.GreedyVAgent,
                    scenario: callable = make_1v1_pistol_flame,
                    seed: int = 1) -> Tuple[np.ndarray, np.ndarray, int]:
    """Run one fight where ``agent`` plays both sides. Capture state
    features at every AI callback entry, then back-fill the eventual
    outcome label per captured state's team perspective.

    Returns:
        states  -- (N, FEATURE_DIM) float32 array, one row per
                   decision-point.
        labels  -- (N,) float32 array of eventual outcomes:
                       +1 if active team won
                       -1 if active team lost
                        0 if draw
        winner  -- the engine's winner team id (1, 2, or -1).
    """
    eng = scenario(seed)
    agent_mod._callback_engine = eng

    captured_feats: list[np.ndarray] = []
    captured_teams: list[int] = []

    def cb(idx: int, turn: int) -> int:
        # Snapshot state features at the START of each leek's turn,
        # *before* the agent decides anything. The label that will
        # be back-filled describes "from this state, did my team
        # eventually win?" -- which is exactly what V should learn.
        feats = np.zeros(FEATURE_DIM, dtype=np.float32)
        eng.extract_v_features(idx, feats)
        captured_feats.append(feats)
        captured_teams.append(eng.entity_team(idx))
        return agent.play_turn(eng, idx, turn)

    eng.set_ai_callback(cb)
    eng.run()

    winner = int(eng.winner)
    n = len(captured_feats)
    states = np.zeros((n, FEATURE_DIM), dtype=np.float32) if n else np.zeros((0, FEATURE_DIM), dtype=np.float32)
    labels = np.zeros(n, dtype=np.float32)
    for i in range(n):
        states[i] = captured_feats[i]
        team = captured_teams[i]
        if winner == -1:
            labels[i] = 0.0
        elif team == winner:
            labels[i] = 1.0
        else:
            labels[i] = -1.0
    return states, labels, winner


# =========================================================== replay buf ==


class ReplayBuffer:
    """Fixed-capacity FIFO buffer over (state_features, label) pairs.

    Pre-allocates two numpy arrays so push() and sample() are
    pointer-arithmetic only -- no per-call allocation, no copies
    beyond the initial fill.
    """

    def __init__(self, capacity: int = 1_000_000,
                  feature_dim: int = FEATURE_DIM,
                  rng: Optional[np.random.Generator] = None):
        self.capacity = capacity
        self.feature_dim = feature_dim
        self.states = np.zeros((capacity, feature_dim), dtype=np.float32)
        self.labels = np.zeros(capacity, dtype=np.float32)
        self.write = 0      # next write index
        self.size  = 0      # number of valid entries
        self.rng   = rng if rng is not None else np.random.default_rng()

    def push(self, states: np.ndarray, labels: np.ndarray) -> None:
        """Append ``states`` (N, F) and ``labels`` (N,) into the
        buffer. Wraps around when capacity is reached (FIFO eviction).
        """
        n = states.shape[0]
        if n == 0:
            return
        if n >= self.capacity:
            # Pathological: new chunk is bigger than the whole buffer.
            # Keep only the last ``capacity`` entries.
            states = states[-self.capacity:]
            labels = labels[-self.capacity:]
            n = self.capacity

        end = self.write + n
        if end <= self.capacity:
            self.states[self.write:end] = states
            self.labels[self.write:end] = labels
        else:
            # Wrap.
            head = self.capacity - self.write
            self.states[self.write:self.capacity] = states[:head]
            self.labels[self.write:self.capacity] = labels[:head]
            tail = n - head
            self.states[:tail] = states[head:]
            self.labels[:tail] = labels[head:]
        self.write = end % self.capacity
        self.size = min(self.capacity, self.size + n)

    def sample(self, batch_size: int) -> Tuple[np.ndarray, np.ndarray]:
        """Uniform random sample (with replacement) of ``batch_size``
        rows. Returns shallow views (zero-copy) -- caller must not
        write to them."""
        if self.size == 0:
            raise ValueError("replay buffer is empty")
        idx = self.rng.integers(0, self.size, size=batch_size)
        return self.states[idx], self.labels[idx]

    def __len__(self) -> int:
        return self.size


# ============================================================ benchmark ==
# Simple loop to gauge per-fight throughput; intended for ad-hoc
# perf checks, not part of the training pipeline.

if __name__ == "__main__":
    import time
    import torch

    from leekzero.model import MLPv1

    torch.manual_seed(0)
    model = MLPv1()
    a = agent_mod.GreedyVAgent(
        model, weapons=[WEAPON_PISTOL], chips=[CHIP_FLAME],
        max_actions_per_turn=20, temperature=0.5, epsilon=0.5)
    buf = ReplayBuffer(capacity=100_000)

    N = 100
    total_samples = 0
    wins_per_team = {1: 0, 2: 0, -1: 0}
    t0 = time.perf_counter()
    for k in range(N):
        states, labels, winner = play_one_fight(a, seed=k)
        buf.push(states, labels)
        total_samples += states.shape[0]
        wins_per_team[winner] = wins_per_team.get(winner, 0) + 1
    dt = time.perf_counter() - t0
    print(f"Played {N} fights in {dt:.2f} s ({N/dt:.1f} fights/s)")
    print(f"  total samples:  {total_samples}")
    print(f"  buffer size:    {len(buf)}")
    print(f"  wins by team:   {wins_per_team}")
