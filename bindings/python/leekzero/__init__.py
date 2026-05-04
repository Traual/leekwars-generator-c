"""Leek-Zero: AlphaZero-style self-play training on the C engine.

Architecture:
  features --> [MLP V-network] --> V(state)
                                       |
            +--------------------------+
            |
   [V-guided beam agent]   <-- clones engine, applies candidate
            |                  actions, scores resulting states
            v
   [self-play workers] --> (state, eventual_outcome) tuples
            |
            v
        [replay buffer]
            |
            v
        [trainer] --> updated V weights
            |
            v
       [eval gate] --> promote if win-rate > 55% vs current best

Modules:
  model.py     -- MLPv1: 256 -> 256 -> 128 -> 1, ~100K params
  features.py  -- thin numpy wrappers around eng.extract_v_features
  agent.py     -- V-guided beam-search agent (TODO)
  selfplay.py  -- multiprocessing self-play workers (TODO)
  trainer.py   -- training loop + eval gate (TODO)
"""

__version__ = "0.0.1"
