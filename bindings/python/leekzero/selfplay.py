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
                    seed: int = 1,
                    label_mode: str = "margin") -> Tuple[np.ndarray, np.ndarray, int]:
    """Run one fight where ``agent`` plays both sides. Capture state
    features at every AI callback entry, then back-fill the eventual
    outcome label per captured state's team perspective.

    label_mode:
      "binary" -- pure +1/-1/0 from the winner field. Fast and
                  simple but very low signal: every state in the
                  same fight gets the same label.
      "margin" -- signed HP-margin at end-of-fight, scaled to
                  [-1, +1]:
                      margin = (my_team_HP - enemy_team_HP) / total_initial
                  A clean wipe (enemy at 0, you full HP) gives ~+1; a
                  pure exchange of blows ends near 0 even if you
                  technically "won". Much richer training signal
                  than binary, since states that lead to dominant
                  positions are scored higher than states that
                  squeak by. Default.

    Returns:
        states  -- (N, FEATURE_DIM) float32 array, one row per
                   decision-point.
        labels  -- (N,) float32 array of outcomes from each captured
                   state's team perspective, in [-1, +1].
        winner  -- the engine's winner team id (1, 2, or -1).
    """
    eng = scenario(seed)
    agent_mod._callback_engine = eng

    captured_feats: list[np.ndarray] = []
    captured_teams: list[int] = []

    # Snapshot total initial life per team so we can compute the
    # margin label after the fight.
    n_ent = eng.n_entities()
    initial_life_by_team: dict[int, int] = {}
    for i in range(n_ent):
        team = eng.entity_team(i)
        initial_life_by_team[team] = initial_life_by_team.get(team, 0) + eng.entity_hp(i)

    def cb(idx: int, turn: int) -> int:
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

    if label_mode == "margin":
        # Final HP per team, normalized by *total* initial life
        # (sum across both teams). The label per captured state's
        # team perspective is signed: positive when my team has more
        # HP at the end. This yields a continuous target even when
        # the winner field reports a draw (e.g., max-turn timeout).
        final_life: dict[int, int] = {}
        for i in range(eng.n_entities()):
            t = eng.entity_team(i)
            if eng.entity_alive(i):
                final_life[t] = final_life.get(t, 0) + eng.entity_hp(i)
            else:
                final_life.setdefault(t, 0)
        total_initial = max(sum(initial_life_by_team.values()), 1)
        for i in range(n):
            team = captured_teams[i]
            my_final  = final_life.get(team, 0)
            enemy_final = sum(v for t, v in final_life.items() if t != team)
            margin = (my_final - enemy_final) / total_initial
            # Clamp to [-1, +1] just in case (numerical safety).
            labels[i] = float(max(-1.0, min(1.0, margin)))
            states[i] = captured_feats[i]
    else:
        # Binary fallback for ablations.
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


# ============================================== self-play (hybrid model) ==


# Spatial tensor shape; mirrors lw_features.h.
SPATIAL_SHAPE = (4, 18, 35)
SPATIAL_TOTAL = 4 * 18 * 35   # 2520


def play_one_fight_hybrid(agent,
                            scenario: callable = make_1v1_pistol_flame,
                            seed: int = 1,
                            label_mode: str = "margin"):
    """Hybrid variant of ``play_one_fight``: also captures the
    spatial tensor at each callback. Returns (entity_states,
    spatial_states, labels, winner).

    The spatial array is shape (N, C, H, W) ready to feed into
    HybridV1 after ``torch.from_numpy``.
    """
    eng = scenario(seed)
    agent_mod._callback_engine = eng

    captured_ent: list[np.ndarray] = []
    captured_spt: list[np.ndarray] = []
    captured_teams: list[int] = []

    n_ent_initial = eng.n_entities()
    initial_life: dict[int, int] = {}
    for i in range(n_ent_initial):
        team = eng.entity_team(i)
        initial_life[team] = initial_life.get(team, 0) + eng.entity_hp(i)

    def cb(idx: int, turn: int) -> int:
        e = np.zeros(FEATURE_DIM, dtype=np.float32)
        s = np.zeros(SPATIAL_TOTAL, dtype=np.float32)
        eng.extract_v_features(idx, e)
        eng.extract_spatial_features(idx, s)
        captured_ent.append(e)
        captured_spt.append(s)
        captured_teams.append(eng.entity_team(idx))
        return agent.play_turn(eng, idx, turn)

    eng.set_ai_callback(cb)
    eng.run()

    winner = int(eng.winner)
    n = len(captured_ent)
    ent_arr = np.zeros((n, FEATURE_DIM), dtype=np.float32) if n else np.zeros((0, FEATURE_DIM), dtype=np.float32)
    spt_arr = np.zeros((n, *SPATIAL_SHAPE), dtype=np.float32) if n else np.zeros((0, *SPATIAL_SHAPE), dtype=np.float32)
    labels  = np.zeros(n, dtype=np.float32)

    if label_mode == "margin":
        final_life: dict[int, int] = {}
        for i in range(eng.n_entities()):
            t = eng.entity_team(i)
            if eng.entity_alive(i):
                final_life[t] = final_life.get(t, 0) + eng.entity_hp(i)
            else:
                final_life.setdefault(t, 0)
        total_initial = max(sum(initial_life.values()), 1)
        for i in range(n):
            ent_arr[i] = captured_ent[i]
            spt_arr[i] = captured_spt[i].reshape(SPATIAL_SHAPE)
            team = captured_teams[i]
            my_final  = final_life.get(team, 0)
            enemy_final = sum(v for t, v in final_life.items() if t != team)
            margin = (my_final - enemy_final) / total_initial
            labels[i] = float(max(-1.0, min(1.0, margin)))
    else:
        for i in range(n):
            ent_arr[i] = captured_ent[i]
            spt_arr[i] = captured_spt[i].reshape(SPATIAL_SHAPE)
            team = captured_teams[i]
            if winner == -1:
                labels[i] = 0.0
            elif team == winner:
                labels[i] = 1.0
            else:
                labels[i] = -1.0
    return ent_arr, spt_arr, labels, winner


class HybridReplayBuffer:
    """Replay buffer for the Hybrid network: stores both spatial and
    entity tensors. Default capacity 100K samples ~= 1 GB
    (mostly the spatial tensor at 10 KB/sample). Tune with the
    ``capacity`` arg.
    """

    def __init__(self, capacity: int = 100_000,
                  feature_dim: int = FEATURE_DIM,
                  spatial_shape: tuple = SPATIAL_SHAPE,
                  rng: Optional[np.random.Generator] = None):
        self.capacity = capacity
        self.feature_dim = feature_dim
        self.spatial_shape = spatial_shape
        self.entity = np.zeros((capacity, feature_dim), dtype=np.float32)
        self.spatial = np.zeros((capacity, *spatial_shape), dtype=np.float32)
        self.labels  = np.zeros(capacity, dtype=np.float32)
        self.write = 0
        self.size = 0
        self.rng = rng if rng is not None else np.random.default_rng()

    def push(self, entity: np.ndarray, spatial: np.ndarray,
              labels: np.ndarray) -> None:
        n = entity.shape[0]
        if n == 0: return
        if n >= self.capacity:
            entity  = entity[-self.capacity:]
            spatial = spatial[-self.capacity:]
            labels  = labels[-self.capacity:]
            n = self.capacity
        end = self.write + n
        if end <= self.capacity:
            self.entity[self.write:end] = entity
            self.spatial[self.write:end] = spatial
            self.labels[self.write:end] = labels
        else:
            head = self.capacity - self.write
            self.entity[self.write:self.capacity] = entity[:head]
            self.spatial[self.write:self.capacity] = spatial[:head]
            self.labels[self.write:self.capacity] = labels[:head]
            tail = n - head
            self.entity[:tail] = entity[head:]
            self.spatial[:tail] = spatial[head:]
            self.labels[:tail] = labels[head:]
        self.write = end % self.capacity
        self.size = min(self.capacity, self.size + n)

    def sample(self, batch_size: int):
        if self.size == 0:
            raise ValueError("replay buffer is empty")
        idx = self.rng.integers(0, self.size, size=batch_size)
        return self.entity[idx], self.spatial[idx], self.labels[idx]

    def __len__(self) -> int:
        return self.size


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
