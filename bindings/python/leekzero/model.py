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


# ============================================================ HybridV1 ===

# Constants kept in lock-step with lw_features.h. The Cython binding
# reports them at runtime via Engine.spatial_dim().
SPATIAL_C = 4
SPATIAL_H = 18
SPATIAL_W = 35


class HybridV1(nn.Module):
    """Two-branch value network for stage-2+:

      * Spatial branch: small CNN (4 channels: walkable / my / enemy
        / dead) over the 18 x 35 cell grid. Strided convs collapse
        to a 32-d global descriptor.
      * Entity branch: MLP over the 256-d per-entity feature buffer.
        Captures stats / hp / tp / mp / cooldowns more efficiently
        than the CNN can (no spatial structure to per-stat fields).
      * Fusion head: concat 32 + 128 = 160, MLP, tanh.

    Sizing:
      ~125 K params total
      ~370 K mul / forward (4x MLPv1, ~240 us CPU)

    The spatial input is consumed straight from the Cython binding's
    ``Engine.extract_spatial_features`` -- the buffer arrives in
    (4, 18, 35) NCHW order, ready for nn.Conv2d.
    """

    def __init__(self):
        super().__init__()

        # Spatial branch: 4 -> 16 (s=2) -> 32 (s=2) -> AvgPool -> 32-d
        self.cnn = nn.Sequential(
            nn.Conv2d(SPATIAL_C, 16, kernel_size=3, stride=2, padding=1),
            nn.ReLU(inplace=True),
            nn.Conv2d(16, 32, kernel_size=3, stride=2, padding=1),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d(1),     # collapses to (B, 32, 1, 1)
            nn.Flatten(),                # -> (B, 32)
        )

        # Entity branch: same shape as MLPv1 minus the head.
        self.entity = nn.Sequential(
            nn.Linear(FEATURE_DIM, 256),
            nn.LayerNorm(256),
            nn.ReLU(inplace=True),
            nn.Linear(256, 128),
            nn.ReLU(inplace=True),
        )

        # Fusion: concat (32 + 128) = 160 -> 128 -> 1.
        self.head = nn.Sequential(
            nn.Linear(32 + 128, 128),
            nn.ReLU(inplace=True),
            nn.Linear(128, 1),
        )

    def forward(self, spatial: torch.Tensor,
                  entity: torch.Tensor) -> torch.Tensor:
        # spatial: (B, 4, 18, 35) or (4, 18, 35)
        # entity:  (B, 256)        or (256,)
        if spatial.dim() == 3:
            spatial = spatial.unsqueeze(0)
            squeeze = True
        else:
            squeeze = False
        if entity.dim() == 1:
            entity = entity.unsqueeze(0)

        s = self.cnn(spatial)
        e = self.entity(entity)
        h = torch.cat([s, e], dim=-1)
        v = torch.tanh(self.head(h)).squeeze(-1)
        if squeeze:
            v = v.squeeze(0)
        return v


if __name__ == "__main__":
    # Quick sanity check on both architectures.
    m = MLPv1()
    print(model_info(m))
    x = torch.randn(8, FEATURE_DIM)
    v = m(x)
    print(f"\nforward(8x256) -> v.shape = {tuple(v.shape)}, range [{v.min():.3f}, {v.max():.3f}]")

    print()
    h = HybridV1()
    print(model_info(h))
    sp = torch.randn(8, SPATIAL_C, SPATIAL_H, SPATIAL_W)
    en = torch.randn(8, FEATURE_DIM)
    v = h(sp, en)
    print(f"\nforward(spatial={tuple(sp.shape)}, entity={tuple(en.shape)}) "
          f"-> v.shape = {tuple(v.shape)}, range [{v.min():.3f}, {v.max():.3f}]")
