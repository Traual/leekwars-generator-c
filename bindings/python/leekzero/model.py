"""V-network architecture for Leek-Zero.

A small MLP V(state) → scalar in [-1, +1]. State input is the 256-d
feature vector produced by ``Engine.extract_v_features`` -- 16
entity slots × 16 fields each.

Constraints driving the design:

* **LeekScript deployable** -- target leekwars.com production with
  a 10M mul/leek-turn budget. We need plain Linear+ReLU; no
  Conv2d, no Transformer (those don't translate cleanly to the
  LeekScript runtime and explode the multiplication count).
* **CPU forward fast** -- the bottleneck during training is the
  neural-net forward inside MCTS/beam rollouts. An MLP with ~100K
  params runs in ~50µs on a 5600X CPU thread.
* **Identical architecture train/deploy** -- weights dump straight
  to LeekScript arrays without distillation, which avoids the
  "teacher vs student" drift trap.

Architecture (V1):
    Input: 256
    Linear 256 -> 256 + LayerNorm + ReLU
    Linear 256 -> 128 + ReLU
    Linear 128 -> 1   + tanh   (scalar in [-1, +1])
    Total params: ~98 K
    Forward mul cost: ~98 K mul (well under 1M -> 100x headroom
    in the LeekScript budget per decision)

Output convention:
    +1  : the active team is winning (will win this fight)
    -1  : the active team is losing
     0  : draw / undetermined
"""
from __future__ import annotations

import torch
import torch.nn as nn


# Constants kept in sync with lw_features.h. The Cython binding
# also exposes Engine.feature_dim() at runtime; we hardcode here
# so model code doesn't need to import the engine just to know
# its input shape.
FEATURE_DIM = 256


class MLPv1(nn.Module):
    """4-layer MLP value network. ~98K params.

    The output passes through tanh to bound it in [-1, +1] -- self-
    play targets are ±1 (win/loss) or scaled outcome margin, both
    naturally bounded.
    """

    def __init__(self, in_dim: int = FEATURE_DIM, hidden1: int = 256,
                  hidden2: int = 128):
        super().__init__()
        self.fc1 = nn.Linear(in_dim, hidden1)
        self.ln1 = nn.LayerNorm(hidden1)
        self.fc2 = nn.Linear(hidden1, hidden2)
        self.head = nn.Linear(hidden2, 1)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x: (B, 256) or (256,)
        if x.dim() == 1:
            x = x.unsqueeze(0)
            squeeze = True
        else:
            squeeze = False
        h = torch.relu(self.ln1(self.fc1(x)))
        h = torch.relu(self.fc2(h))
        v = torch.tanh(self.head(h)).squeeze(-1)
        if squeeze:
            v = v.squeeze(0)
        return v


def count_params(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters() if p.requires_grad)


def model_info(model: nn.Module) -> str:
    lines = [f"{type(model).__name__}: {count_params(model):,} params"]
    for name, p in model.named_parameters():
        lines.append(f"  {name:30s} {tuple(p.shape)} -- {p.numel():,}")
    return "\n".join(lines)


if __name__ == "__main__":
    # Quick sanity check.
    m = MLPv1()
    print(model_info(m))
    x = torch.randn(8, FEATURE_DIM)
    v = m(x)
    print(f"\nforward(8x256) -> v.shape = {tuple(v.shape)}, range [{v.min():.3f}, {v.max():.3f}]")
