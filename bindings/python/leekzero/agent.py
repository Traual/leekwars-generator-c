"""V-guided greedy agent for Leek-Zero self-play.

At each atomic decision point inside an AI callback, the agent:

  1. Enumerates candidate next-actions (move, fire weapon, cast chip,
     end turn).
  2. For each candidate, clones the engine, applies the action, and
     extracts the resulting state features.
  3. Batches all features through the V-network in one forward pass
     and picks the action whose resulting V(state) is highest.
  4. Applies that action on the REAL engine and loops until END_TURN
     wins the argmax (or no candidates are legal).

This is depth-1 beam search (greedy on V). It's simpler than full
MCTS and avoids the policy-head training, which is the right
tradeoff for a network we want to deploy on LeekScript.

Action vocabulary (atomic):
  END_TURN                         - bail out of the turn
  MOVE_TOWARD(target_cell, max_mp) - eng.move_toward
  FIRE(weapon_id, target_cell)     - eng.fire_weapon
  CAST(chip_id, target_cell)       - eng.use_chip

The agent generates one candidate per (verb, target) pair that's
reasonable for the situation: move toward each living enemy, fire
each known weapon at each enemy's cell, cast each known chip at
self or at each enemy.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, List, Optional, Sequence

import numpy as np
import torch


# Action verb tags. We keep them as ints so callsites can switch
# without string comparisons in hot loops.
END_TURN     = 0
MOVE_TOWARD  = 1
FIRE         = 2
CAST         = 3


@dataclass
class Action:
    """One atomic action for the agent to apply.

    Fields are populated based on `verb`:
      END_TURN     -> nothing else
      MOVE_TOWARD  -> target_cell, max_mp
      FIRE         -> weapon_id, target_cell
      CAST         -> chip_id, target_cell
    """
    verb: int
    target_cell: int = -1
    weapon_id: int = -1
    chip_id: int = -1
    max_mp: int = 6


def apply_action(eng, idx: int, action: Action) -> int:
    """Apply ``action`` to the engine. Return the engine rc (>=1 on
    success; 0 or negative on failure / illegal). END_TURN returns 1
    unconditionally (the agent's signal to stop).
    """
    if action.verb == END_TURN:
        return 1
    if action.verb == MOVE_TOWARD:
        return eng.move_toward(idx, action.target_cell, max_mp=action.max_mp)
    if action.verb == FIRE:
        return eng.fire_weapon(idx, action.weapon_id, action.target_cell)
    if action.verb == CAST:
        return eng.use_chip(idx, action.chip_id, action.target_cell)
    return 0


def enumerate_candidates(eng, idx: int,
                          weapons: Sequence[int],
                          chips: Sequence[int],
                          *,
                          include_move: bool = True,
                          include_self_cast: bool = True) -> List[Action]:
    """Build a flat list of candidate actions for entity ``idx`` at
    the current state.

    The enumeration is intentionally generous -- illegal candidates
    (out of TP, out of range, on cooldown, ...) are filtered later
    by the engine's atomic apply. The cost of clone + score on a
    failing candidate is low enough that we don't bother with
    pre-validation.
    """
    n = eng.n_entities()
    my_team = eng.entity_team(idx)
    my_cell = eng.entity_cell(idx)

    enemies: List[int] = []
    for i in range(n):
        if i == idx or not eng.entity_alive(i):
            continue
        if eng.entity_team(i) == my_team:
            continue
        enemies.append(eng.entity_cell(i))

    out: List[Action] = [Action(verb=END_TURN)]

    if include_move:
        for cell in enemies:
            out.append(Action(verb=MOVE_TOWARD, target_cell=cell, max_mp=6))

    for w in weapons:
        for cell in enemies:
            out.append(Action(verb=FIRE, weapon_id=w, target_cell=cell))

    for c in chips:
        # On self (most chips: heal, buff, shield, ...).
        if include_self_cast:
            out.append(Action(verb=CAST, chip_id=c, target_cell=my_cell))
        # On enemy (damage chips, debuffs, poison, ...).
        for cell in enemies:
            out.append(Action(verb=CAST, chip_id=c, target_cell=cell))

    return out


class GreedyVAgent:
    """V-guided depth-1 beam (a.k.a. greedy on V) with exploration.

    Per atomic decision:
      score(s, a) = V(state_after_action(s, a))
      either argmax (greedy) or temperature-softmax sampling

    Exploration knobs critical for self-play:
      ``temperature``: 0.0 = pure argmax. >0 = sample from softmax(v/T).
        T=1.0 is the typical AlphaZero self-play temperature for the
        early-game; we drop to 0 for late-game / eval.
      ``epsilon``: probability of overriding the chosen action with a
        UNIFORM RANDOM non-END_TURN action. This guarantees we explore
        actual combat actions even with a near-zero / uninformative V.

    Without exploration, an untrained V picks END_TURN on the first
    decision (random tie-break favors candidate 0) and the fight
    produces no labeled (state, outcome) data -- you can't bootstrap
    self-play from there.

    The agent owns an ``MLPv1`` model and a scratch numpy buffer for
    feature extraction. ``play_turn`` is called from inside the
    engine's AI callback.
    """

    def __init__(self, model: torch.nn.Module,
                  weapons: Sequence[int],
                  chips: Sequence[int],
                  *,
                  feature_dim: int = 256,
                  max_actions_per_turn: int = 32,
                  device: str = "cpu",
                  temperature: float = 0.0,
                  epsilon: float = 0.0,
                  rng: Optional[np.random.Generator] = None):
        self.model = model.to(device).eval()
        self.weapons = list(weapons)
        self.chips = list(chips)
        self.feature_dim = feature_dim
        self.max_actions_per_turn = max_actions_per_turn
        self.device = device
        self.temperature = float(temperature)
        self.epsilon = float(epsilon)
        self.rng = rng if rng is not None else np.random.default_rng()
        # Soft prior added to V values per verb. Helps bootstrap self-
        # play with a random-init V: even when V is uninformative,
        # the agent leans toward combat actions (fire, then cast) over
        # idling. The exact magnitude doesn't matter much -- it just
        # needs to be larger than the random V's standard deviation
        # (~0.05 with tanh + uniform-init weights). Reset to all zeros
        # once V is trained.
        self.action_prior: dict[int, float] = {
            END_TURN:    -0.30,   # discourage early end of turn
            MOVE_TOWARD:  0.00,
            CAST:         0.10,
            FIRE:         0.20,   # bias toward firing in stage 1
        }

    @torch.no_grad()
    def _score_candidates(self, eng, idx: int,
                            candidates: List[Action]) -> np.ndarray:
        """Clone + apply + extract for each candidate; batch through
        the V network; return a 1D numpy array of values (length =
        len(candidates)). EndTurn is special-cased to score the
        current state (no clone needed)."""
        feats = np.zeros((len(candidates), self.feature_dim), dtype=np.float32)
        for i, a in enumerate(candidates):
            if a.verb == END_TURN:
                eng.extract_v_features(idx, feats[i])
                continue
            # Clone, apply, extract.
            c = eng.clone()
            rc = apply_action(c, idx, a)
            if rc <= 0:
                # Illegal action: penalize so it's never picked unless
                # nothing else works. We still score the unmutated
                # state so the network sees something coherent.
                eng.extract_v_features(idx, feats[i])
                feats[i, 0] = -1.0   # mark slot 0 with sentinel; downstream
                                      # post-processing applies the penalty.
                continue
            c.extract_v_features(idx, feats[i])
            # `c` will be GC'd; its __dealloc__ frees the cloned heap.

        # Batched forward.
        x = torch.from_numpy(feats).to(self.device)
        v = self.model(x).detach().cpu().numpy().astype(np.float32, copy=False)

        # Apply illegal-action penalty + the soft per-verb prior.
        for i, a in enumerate(candidates):
            if a.verb != END_TURN and feats[i, 0] == -1.0:
                v[i] = -1e6
            else:
                v[i] += self.action_prior.get(a.verb, 0.0)
        return v

    def _select(self, candidates: List[Action], values: np.ndarray) -> int:
        """Pick one candidate index given values and the configured
        temperature/epsilon. Returns an index into ``candidates``."""
        # Epsilon-exploration: with probability ``epsilon``, override
        # whatever V says with a UNIFORM RANDOM non-END_TURN action.
        # This is the key knob that bootstraps self-play from a
        # random-init V network.
        if self.epsilon > 0.0 and self.rng.random() < self.epsilon:
            non_end = [i for i, a in enumerate(candidates) if a.verb != END_TURN]
            if non_end:
                return int(self.rng.choice(non_end))

        if self.temperature <= 0.0:
            return int(np.argmax(values))

        # Temperature-softmax sampling. Numerically stable: subtract
        # the max before exp.
        scaled = values / max(self.temperature, 1e-6)
        scaled -= scaled.max()
        probs = np.exp(scaled, dtype=np.float64)
        probs /= probs.sum()
        return int(self.rng.choice(len(candidates), p=probs))

    def play_turn(self, eng, idx: int, turn: int) -> int:
        """Drive the AI callback for entity ``idx`` on ``turn``.

        Returns the number of atomic actions applied (purely for
        diagnostic / logging purposes; the engine ignores it).
        """
        applied = 0
        while applied < self.max_actions_per_turn:
            candidates = enumerate_candidates(
                eng, idx, self.weapons, self.chips)
            if not candidates:
                break
            values = self._score_candidates(eng, idx, candidates)
            best = self._select(candidates, values)
            chosen = candidates[best]
            if chosen.verb == END_TURN:
                break
            rc = apply_action(eng, idx, chosen)
            if rc <= 0:
                # Engine refused; bail out to avoid infinite loops.
                break
            applied += 1
        return applied


# Convenience factory used by self-play workers / scenario runners.

def build_callback(agent: GreedyVAgent):
    """Return an AI-callback closure compatible with
    ``Engine.set_ai_callback``."""
    def cb(idx, turn):
        return agent.play_turn(_callback_engine, idx, turn)
    # ``_callback_engine`` is a module-level slot the harness fills
    # in just before set_ai_callback. This keeps the closure simple
    # and avoids capturing the engine via cell variable, which can
    # interact badly with multiprocessing pickling.
    return cb


# Slot the harness writes to (see selfplay.py). Lives at module
# level so build_callback can refer to it without late-binding
# games.
_callback_engine = None
