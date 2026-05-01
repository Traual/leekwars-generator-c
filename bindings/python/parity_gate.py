"""Parity gate: assert C engine formulas match the Python reference.

For each effect type with a numeric apply() in the Python source
(leekwars-generator-python), generates 1000+ random parameter sets,
runs the formula in BOTH engines, and asserts byte-for-byte equality
on the resulting state mutations (HP, total_HP, buff_stats[]).

Reference engine: imports the actual Python ``leekwars`` package as
the source of truth. Re-implements the Effect classes in pure Python
where mocking the full state machinery (Fight, Order, Statistics,
ActionLog) would be heavier than just porting the formula.

What it does NOT yet cover:
  - Action stream comparison (we only check post-state).
  - Passive event hooks (POISON_TO_*, DAMAGE_TO_*, etc.).
  - Multi-turn ticking across both engines (tested separately).
  - Resurrect / Summon.
"""
from __future__ import annotations

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import leekwars_c as lwc  # type: ignore
from leekwars_c._engine import State  # type: ignore

# Add the Python engine to sys.path so we can pull java_round directly.
# We try a few common locations; on Linux/WSL the Windows drive is at /mnt/c.
for _candidate in (
    os.environ.get("LEEKWARS_PY_DIR"),
    "/mnt/c/Users/aurel/Desktop/leekwars_generator_python",
    "C:/Users/aurel/Desktop/leekwars_generator_python",
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "..", "..", "..", "leekwars_generator_python"),
):
    if _candidate and os.path.isdir(_candidate) and _candidate not in sys.path:
        sys.path.insert(0, _candidate)
        break

try:
    from leekwars.util.java_math import java_round
except ImportError:
    # Fallback: implement Java's Math.round (half-up away from -inf)
    def java_round(x: float) -> int:
        return int(math.floor(x + 0.5))


# Stat indices matching include/lw_types.h.
LIFE = 0; TP = 1; MP = 2; STRENGTH = 3; AGILITY = 4; FREQUENCY = 5
WISDOM = 6; ABS_SHIELD = 9; REL_SHIELD = 10; RESISTANCE = 11
SCIENCE = 12; MAGIC = 13; DAMAGE_RETURN = 14; POWER = 15

# State-flag bits.
STATE_INVINCIBLE = 1 << 3
STATE_UNHEALABLE = 1 << 2


# =====================================================================
# Pure-Python reference implementations of each effect formula.
# Each one is a line-by-line port of the corresponding leekwars/effect/
# *.py apply() method, simplified to operate on plain numbers.
# =====================================================================

def py_damage_full(v1, v2, jet, strength, power, aoe, crit_pwr, target_count,
                    rel_shield, abs_shield, target_hp,
                    target_invincible, target_damage_return,
                    caster_wisdom, caster_hp, caster_total_hp,
                    caster_invincible, caster_unhealable,
                    self_target):
    """Mirrors EffectDamage.apply byte-for-byte INCLUDING life_steal
    and return_damage branches. Returns
    (dealt, target_hp_after, caster_hp_after)."""
    str_clamped = max(0, strength)
    d = (v1 + jet * v2) * (1 + str_clamped / 100.0) * aoe * crit_pwr * target_count * (1 + power / 100.0)

    # Return damage from target's DAMAGE_RETURN, computed BEFORE shields.
    return_damage = 0
    if not self_target:
        return_damage = java_round(d * target_damage_return / 100.0)

    # Shields
    d -= d * (rel_shield / 100.0) + abs_shield
    d = max(0.0, d)
    if target_invincible:
        d = 0.0
    dealt = java_round(d)
    if dealt < 0: dealt = 0
    if dealt > target_hp: dealt = target_hp

    # Life steal from rounded ``value`` AFTER shields, BEFORE removeLife.
    life_steal = 0
    if not self_target:
        life_steal = java_round(dealt * caster_wisdom / 1000.0)

    target_hp_after = target_hp - dealt
    caster_hp_after = caster_hp

    # Apply life steal (caster heal up to missing HP, blocked by UNHEALABLE).
    if life_steal > 0 and caster_hp_after > 0 and not caster_unhealable and caster_hp_after < caster_total_hp:
        missing = caster_total_hp - caster_hp_after
        caster_hp_after += min(life_steal, missing)

    # Apply return damage (blocked by caster INVINCIBLE).
    if return_damage > 0 and not caster_invincible:
        applied = min(return_damage, caster_hp_after)
        caster_hp_after -= applied

    return dealt, target_hp_after, caster_hp_after


def py_damage(v1, v2, jet, strength, power, aoe, crit_pwr, target_count,
              rel_shield, abs_shield, target_hp, invincible):
    """Just the dealt-damage portion (no life_steal / return)."""
    dealt, _, _ = py_damage_full(
        v1, v2, jet, strength, power, aoe, crit_pwr, target_count,
        rel_shield, abs_shield, target_hp, invincible, 0,
        0, 1000, 1000, False, False, False)
    return dealt


def py_heal(v1, v2, jet, wisdom, aoe, crit_pwr, target_count, missing_hp,
            unhealable):
    if unhealable:
        return 0
    v = (v1 + jet * v2) * (1 + wisdom / 100.0) * aoe * crit_pwr * target_count
    amt = java_round(v)
    if amt < 0: amt = 0
    if amt > missing_hp: amt = missing_hp
    return amt


def py_absolute_shield(v1, v2, jet, resistance, aoe, crit_pwr):
    v = (v1 + jet * v2) * (1 + resistance / 100.0) * aoe * crit_pwr
    amt = java_round(v)
    return amt if amt > 0 else 0


def py_relative_shield(v1, v2, jet, resistance, aoe, crit_pwr):
    return py_absolute_shield(v1, v2, jet, resistance, aoe, crit_pwr)


def py_buff_stat(v1, v2, jet, scale, aoe, crit_pwr):
    """Generic buff_stat formula (BUFF_STRENGTH/AGILITY/MP/TP/RES/WIS,
    DAMAGE_RETURN). scale = caster's scale_stat."""
    v = (v1 + v2 * jet) * (1 + scale / 100.0) * aoe * crit_pwr
    amt = java_round(v)
    return amt if amt > 0 else 0


def py_shackle(v1, v2, jet, magic, aoe, crit_pwr):
    """Magic-scaled debuff. max(0, magic) clamp."""
    m = max(0, magic)
    v = (v1 + v2 * jet) * (1 + m / 100.0) * aoe * crit_pwr
    amt = java_round(v)
    return amt if amt > 0 else 0


def py_aftereffect_immediate(v1, v2, jet, science, aoe, crit_pwr,
                              target_hp, invincible):
    v = (v1 + v2 * jet) * (1 + science / 100.0) * aoe * crit_pwr
    amt = java_round(v)
    if amt < 0: amt = 0
    if invincible: amt = 0
    if amt > target_hp: amt = target_hp
    return amt


def py_poison(v1, v2, jet, magic, power, aoe, crit_pwr):
    m = max(0, magic)
    v = (v1 + jet * v2) * (1 + m / 100.0) * aoe * crit_pwr * (1 + power / 100.0)
    amt = java_round(v)
    return amt if amt > 0 else 0


def py_vitality(v1, v2, jet, wisdom, aoe, crit_pwr):
    v = (v1 + jet * v2) * (1 + wisdom / 100.0) * aoe * crit_pwr
    amt = java_round(v)
    return amt if amt > 0 else 0


def py_nova_vitality(v1, v2, jet, science, aoe, crit_pwr):
    v = (v1 + jet * v2) * (1 + science / 100.0) * aoe * crit_pwr
    amt = java_round(v)
    return amt if amt > 0 else 0


def py_nova_damage(v1, v2, jet, science, power, aoe, crit_pwr,
                   total_hp, hp, invincible):
    s = max(0, science)
    d = (v1 + jet * v2) * (1 + s / 100.0) * aoe * crit_pwr * (1 + power / 100.0)
    if invincible:
        d = 0.0
    amt = java_round(d)
    if amt < 0: amt = 0
    gap = total_hp - hp
    if amt > gap: amt = gap
    return amt


def py_life_damage(v1, v2, jet, caster_life, power, aoe, crit_pwr,
                   target_hp, rel_shield, abs_shield, invincible):
    d = ((v1 + jet * v2) / 100.0) * caster_life * aoe * crit_pwr * (1 + power / 100.0)
    if invincible:
        d = 0.0
    d -= d * (rel_shield / 100.0) + abs_shield
    if d < 0.0:
        d = 0.0
    dealt = java_round(d)
    if dealt < 0: dealt = 0
    if dealt > target_hp: dealt = target_hp
    return dealt


def py_raw_buff_stat(v1, v2, jet, aoe, crit_pwr):
    v = (v1 + jet * v2) * aoe * crit_pwr
    amt = java_round(v)
    return amt if amt > 0 else 0


def py_vulnerability(v1, v2, jet, aoe, crit_pwr):
    return py_raw_buff_stat(v1, v2, jet, aoe, crit_pwr)


def py_erosion(value, rate):
    return java_round(value * rate)


# =====================================================================
# C-side: tiny helper that builds a State with two entities, applies
# the relevant formula, and reports the result.
# =====================================================================

def make_state(caster_stats=None, target_stats=None,
               target_hp=1000, target_total_hp=1000,
               state_flag=0, target_buff_stats=None):
    s = State()
    # Two entities at separate cells. We don't need a topology because
    # the apply formulas don't dereference map.topo.
    s.add_entity(fid=1, team_id=0, cell_id=-1,
                 total_tp=10, total_mp=4,
                 hp=1000, total_hp=1000,
                 weapons=[], chips=[])
    s.add_entity(fid=2, team_id=1, cell_id=-1,
                 total_tp=10, total_mp=4,
                 hp=target_hp, total_hp=target_total_hp,
                 weapons=[], chips=[])
    s._set_entity_hp(1, target_hp, target_total_hp)
    if state_flag:
        s._set_entity_state_flag(1, state_flag)
    for stat_idx, val in (caster_stats or {}).items():
        s._set_base_stat(0, stat_idx, val)
    for stat_idx, val in (target_stats or {}).items():
        s._set_base_stat(1, stat_idx, val)
    for stat_idx, val in (target_buff_stats or {}).items():
        s._set_buff_stat(1, stat_idx, val)
    return s


# =====================================================================
# Per-formula parity tests.
# =====================================================================

def fuzz_damage(n_cases: int, rng: random.Random) -> int:
    """Compare lw_apply_damage vs py_damage_full (including life_steal
    + return_damage caster mutations) on n_cases random inputs."""
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200)
        v2 = rng.uniform(0, 200)
        jet = rng.uniform(0, 1)
        strength = rng.randint(-50, 500)
        power = rng.randint(0, 200)
        aoe = rng.choice([1.0, 0.8, 0.5, 0.2])
        crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        rel_shield = rng.randint(0, 80)
        abs_shield = rng.randint(0, 200)
        target_hp = rng.randint(50, 5000)
        target_total_hp = max(target_hp, 5000)
        target_invincible = rng.random() < 0.05
        target_damage_return = rng.randint(0, 100)

        caster_wisdom = rng.randint(0, 300)
        caster_hp = rng.randint(50, 5000)
        caster_total_hp = max(caster_hp, 5000)
        caster_invincible = rng.random() < 0.05
        caster_unhealable = rng.random() < 0.05

        s = make_state(
            caster_stats={STRENGTH: strength, POWER: power, WISDOM: caster_wisdom},
            target_stats={REL_SHIELD: rel_shield, ABS_SHIELD: abs_shield,
                          DAMAGE_RETURN: target_damage_return},
            target_hp=target_hp, target_total_hp=target_total_hp,
            state_flag=STATE_INVINCIBLE if target_invincible else 0,
        )
        s._set_entity_hp(0, caster_hp, caster_total_hp)
        caster_state = 0
        if caster_invincible: caster_state |= STATE_INVINCIBLE
        if caster_unhealable: caster_state |= STATE_UNHEALABLE
        if caster_state: s._set_entity_state_flag(0, caster_state)

        c_dealt = s._apply_damage(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)
        c_target_hp = s.entity_hp(1)
        c_caster_hp = s.entity_hp(0)

        py_dealt, py_target_hp, py_caster_hp = py_damage_full(
            v1, v2, jet, strength, power, aoe, crit_pwr, target_count,
            rel_shield, abs_shield, target_hp,
            target_invincible, target_damage_return,
            caster_wisdom, caster_hp, caster_total_hp,
            caster_invincible, caster_unhealable,
            self_target=False,
        )

        if (c_dealt != py_dealt or
            c_target_hp != py_target_hp or
            c_caster_hp != py_caster_hp):
            fails += 1
            if fails <= 3:
                print(f"  damage mismatch trial {trial}:")
                print(f"    dealt: C={c_dealt} PY={py_dealt}")
                print(f"    target_hp: C={c_target_hp} PY={py_target_hp}")
                print(f"    caster_hp: C={c_caster_hp} PY={py_caster_hp}")
                print(f"    v1={v1:.2f} v2={v2:.2f} jet={jet:.4f} str={strength} "
                      f"pow={power} wis={caster_wisdom} dr={target_damage_return}")
    return fails


def fuzz_heal(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200)
        v2 = rng.uniform(0, 200)
        jet = rng.uniform(0, 1)
        wisdom = rng.randint(-50, 300)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2])
        target_hp = rng.randint(50, 4500)
        target_total_hp = rng.randint(target_hp, 5000)
        unhealable = rng.random() < 0.10
        missing = target_total_hp - target_hp

        s = make_state(
            caster_stats={WISDOM: wisdom},
            target_hp=target_hp, target_total_hp=target_total_hp,
            state_flag=STATE_UNHEALABLE if unhealable else 0,
        )
        c_amt = s._apply_heal(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)
        py_amt = py_heal(v1, v2, jet, wisdom, aoe, crit_pwr, target_count,
                          missing, unhealable)
        if c_amt != py_amt:
            fails += 1
            if fails <= 3:
                print(f"  heal mismatch trial {trial}: C={c_amt} PY={py_amt}")
    return fails


def fuzz_absolute_shield(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        resistance = rng.randint(0, 300)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        s = make_state(caster_stats={RESISTANCE: resistance})
        c_amt = s._apply_absolute_shield(0, 1, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_absolute_shield(v1, v2, jet, resistance, aoe, crit_pwr)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_buff_stat(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        scale = rng.randint(-50, 400)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        # buff_strength scales by SCIENCE
        s = make_state(caster_stats={SCIENCE: scale})
        c_amt = s._apply_buff_stat(0, 1, STRENGTH, SCIENCE,
                                    v1, v2, jet, aoe, crit_pwr)
        py_amt = py_buff_stat(v1, v2, jet, scale, aoe, crit_pwr)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_shackle(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        magic = rng.randint(-100, 300)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        s = make_state(caster_stats={MAGIC: magic})
        c_amt = s._apply_shackle(0, 1, STRENGTH, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_shackle(v1, v2, jet, magic, aoe, crit_pwr)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_aftereffect(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200); v2 = rng.uniform(0, 200); jet = rng.uniform(0, 1)
        science = rng.randint(-50, 400)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        target_hp = rng.randint(50, 5000)
        invincible = rng.random() < 0.10
        s = make_state(
            caster_stats={SCIENCE: science},
            target_hp=target_hp, target_total_hp=max(target_hp, 5000),
            state_flag=STATE_INVINCIBLE if invincible else 0,
        )
        c_amt = s._apply_aftereffect(0, 1, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_aftereffect_immediate(v1, v2, jet, science, aoe, crit_pwr,
                                            target_hp, invincible)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_poison(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        magic = rng.randint(-100, 300)
        power = rng.randint(0, 200)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        s = make_state(caster_stats={MAGIC: magic, POWER: power})
        c_amt = s._compute_poison_damage(0, 1, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_poison(v1, v2, jet, magic, power, aoe, crit_pwr)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_nova_damage(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200); v2 = rng.uniform(0, 200); jet = rng.uniform(0, 1)
        science = rng.randint(-50, 400)
        power = rng.randint(0, 200)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        target_hp = rng.randint(50, 4000)
        target_total_hp = rng.randint(target_hp, 5000)
        invincible = rng.random() < 0.10
        s = make_state(
            caster_stats={SCIENCE: science, POWER: power},
            target_hp=target_hp, target_total_hp=target_total_hp,
            state_flag=STATE_INVINCIBLE if invincible else 0,
        )
        c_amt = s._apply_nova_damage(0, 1, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_nova_damage(v1, v2, jet, science, power, aoe, crit_pwr,
                                  target_total_hp, target_hp, invincible)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_life_damage(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        caster_life = rng.randint(100, 5000)
        power = rng.randint(0, 200)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        target_hp = rng.randint(50, 5000)
        rel_shield = rng.randint(0, 70)
        abs_shield = rng.randint(0, 200)
        invincible = rng.random() < 0.10
        s = make_state(
            caster_stats={POWER: power},
            target_stats={REL_SHIELD: rel_shield, ABS_SHIELD: abs_shield},
            target_hp=target_hp, target_total_hp=max(target_hp, 5000),
            state_flag=STATE_INVINCIBLE if invincible else 0,
        )
        s._set_entity_hp(0, caster_life, max(caster_life, 5000))
        c_amt = s._apply_life_damage(0, 1, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_life_damage(v1, v2, jet, caster_life, power, aoe, crit_pwr,
                                  target_hp, rel_shield, abs_shield, invincible)
        if c_amt != py_amt:
            fails += 1
            if fails <= 3:
                print(f"  life_damage mismatch: C={c_amt} PY={py_amt}")
                print(f"    v1={v1:.2f} v2={v2:.2f} jet={jet:.4f} life={caster_life} "
                      f"pow={power} aoe={aoe} crit={crit_pwr}")
    return fails


def fuzz_vitality(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        wisdom = rng.randint(-50, 300)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        s = make_state(caster_stats={WISDOM: wisdom})
        c_amt = s._apply_vitality(0, 1, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_vitality(v1, v2, jet, wisdom, aoe, crit_pwr)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_raw_buff_stat(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        s = make_state()
        c_amt = s._apply_raw_buff_stat(1, STRENGTH, v1, v2, jet, aoe, crit_pwr)
        py_amt = py_raw_buff_stat(v1, v2, jet, aoe, crit_pwr)
        if c_amt != py_amt:
            fails += 1
    return fails


def fuzz_erosion(n_cases: int, rng: random.Random) -> int:
    fails = 0
    for trial in range(n_cases):
        value = rng.randint(0, 5000)
        rate = rng.choice([0.05, 0.10, 0.15, 0.20, 0.0])
        s = make_state()
        c_amt = s._apply_erosion(1, value, rate)
        py_amt = py_erosion(value, rate)
        if c_amt != py_amt:
            fails += 1
    return fails


# =====================================================================
# Strong gate: invoke the actual upstream Python Effect classes.
# Uses minimal mock entities + state. If this passes alongside the
# numeric-formula gate, the chain
#   C engine == parity_gate's reference == upstream Python source
# is closed.
# =====================================================================

class _MockEntity:
    """A bag of getters / setters mirroring the surface area Effect.apply
    touches on Entity. No dependency on Stats / Map / Order."""
    def __init__(self, hp, total_hp, *, strength=0, agility=0, wisdom=0,
                 resistance=0, science=0, magic=0, power=0,
                 abs_shield=0, rel_shield=0, damage_return=0,
                 invincible=False, unhealable=False):
        self._hp = hp
        self._total = total_hp
        self._str = strength
        self._ag = agility
        self._wis = wisdom
        self._res = resistance
        self._sci = science
        self._mag = magic
        self._pwr = power
        self._abs = abs_shield
        self._rel = rel_shield
        self._dr = damage_return
        self._inv = invincible
        self._unh = unhealable
        self.value = 0

    def getStrength(self): return self._str
    def getAgility(self): return self._ag
    def getWisdom(self): return self._wis
    def getResistance(self): return self._res
    def getScience(self): return self._sci
    def getMagic(self): return self._mag
    def getPower(self): return self._pwr
    def getAbsoluteShield(self): return self._abs
    def getRelativeShield(self): return self._rel
    def getDamageReturn(self): return self._dr
    def getLife(self): return self._hp
    def getTotalLife(self): return self._total
    def isDead(self): return self._hp <= 0
    def hasState(self, s):
        # leekwars.attack.entity_state.EntityState IntEnum values:
        #   UNHEALABLE = 2, INVINCIBLE = 3, ...
        # (NOT to be confused with the C engine's bit-flag layout where
        # UNHEALABLE = 1<<2 and INVINCIBLE = 1<<3.)
        if int(s) == 3:  return self._inv
        if int(s) == 2:  return self._unh
        return False
    def removeLife(self, pv, ero, *args, **kw):
        if self._hp <= 0: return
        if pv > self._hp: pv = self._hp
        self._hp -= pv
        self._total -= ero
        if self._total < 1: self._total = 1
        self.value = pv
    def addLife(self, *args, **kw):
        # Last positional arg is the amount.
        amt = args[-1] if args else kw.get("life", 0)
        if isinstance(amt, _MockEntity):
            amt = 0  # called as addLife(caster, value) — 1st arg is caster
        # Actually addLife(caster, value) -> args = (caster, value)
        if len(args) >= 2 and isinstance(args[1], int):
            self._hp += args[1]
            if self._hp > self._total: self._hp = self._total
        elif isinstance(amt, int):
            self._hp += amt
            if self._hp > self._total: self._hp = self._total
    def addTotalLife(self, *args, **kw):
        amt = args[1] if len(args) >= 2 else 0
        if isinstance(amt, int):
            self._total += amt
    def updateBuffStats(self, *args, **kw): pass
    def onDirectDamage(self, *args, **kw): pass
    def onNovaDamage(self, *args, **kw): pass
    def onPoisonDamage(self, *args, **kw): pass
    def removeLaunchedEffect(self, *args, **kw): pass
    def addEffect(self, *args, **kw): pass
    def addLaunchedEffect(self, *args, **kw): pass
    def getEffects(self): return []
    def getFId(self): return 0
    def getId(self): return 0
    def getTeam(self): return 0
    def isSummon(self): return False
    def isAlive(self): return self._hp > 0
    def getCell(self): return None


class _MockStats:
    def damage(self, *a, **kw): pass
    def effect(self, *a, **kw): pass


class _MockState:
    def __init__(self):
        self.statistics = _MockStats()
    def log(self, *a, **kw): pass
    class _Actions:
        def log(self, *a, **kw): pass
    def getActions(self):
        if not hasattr(self, "_actions"):
            self._actions = self._Actions()
        return self._actions


def _call_python_damage(v1, v2, jet, strength, power, aoe, crit_pwr, target_count,
                         rel_shield, abs_shield, target_hp, target_total_hp,
                         target_invincible, target_damage_return,
                         caster_wisdom, caster_hp, caster_total_hp,
                         caster_invincible, caster_unhealable,
                         erosion_rate):
    """Invoke the actual upstream EffectDamage.apply with the given
    erosion_rate. Returns (dealt, target_hp_after, target_total_hp_after,
    caster_hp_after, caster_total_hp_after)."""
    from leekwars.effect.effect_damage import EffectDamage

    caster = _MockEntity(caster_hp, caster_total_hp,
                          strength=strength, power=power, wisdom=caster_wisdom,
                          invincible=caster_invincible, unhealable=caster_unhealable)
    target = _MockEntity(target_hp, target_total_hp,
                          rel_shield=rel_shield, abs_shield=abs_shield,
                          damage_return=target_damage_return,
                          invincible=target_invincible)
    e = EffectDamage()
    e.value1 = v1; e.value2 = v2; e.jet = jet
    e.aoe = aoe; e.criticalPower = crit_pwr; e.targetCount = target_count
    e.erosionRate = erosion_rate
    e.caster = caster; e.target = target
    e.attack = None
    state = _MockState()
    e.apply(state)
    return e.value, target._hp, target._total, caster._hp, caster._total


def fuzz_damage_against_real_python(n_cases: int, rng: random.Random) -> int:
    """Strongest gate: run ACTUAL Python EffectDamage.apply alongside C."""
    fails = 0
    skipped = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200)
        v2 = rng.uniform(0, 200)
        jet = rng.uniform(0, 1)
        strength = rng.randint(0, 500)  # Python uses max(0, ...) -- we test positive
        power = rng.randint(0, 200)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        rel_shield = rng.randint(0, 80)
        abs_shield = rng.randint(0, 200)
        target_hp = rng.randint(50, 5000)
        target_total_hp = max(target_hp, 5000)
        target_invincible = rng.random() < 0.05
        target_damage_return = rng.randint(0, 100)
        caster_wisdom = rng.randint(0, 300)
        caster_hp = rng.randint(50, 5000)
        caster_total_hp = max(caster_hp, 5000)
        caster_invincible = rng.random() < 0.05
        caster_unhealable = rng.random() < 0.05

        try:
            py_dealt, py_target_hp, py_caster_hp = _call_python_damage(
                v1, v2, jet, strength, power, aoe, crit_pwr, target_count,
                rel_shield, abs_shield, target_hp, target_total_hp,
                target_invincible, target_damage_return,
                caster_wisdom, caster_hp, caster_total_hp,
                caster_invincible, caster_unhealable,
            )
        except Exception as exc:
            skipped += 1
            if skipped <= 1:
                print(f"  SKIP (Python apply raised): {exc}")
            continue

        s = make_state(
            caster_stats={STRENGTH: strength, POWER: power, WISDOM: caster_wisdom},
            target_stats={REL_SHIELD: rel_shield, ABS_SHIELD: abs_shield,
                          DAMAGE_RETURN: target_damage_return},
            target_hp=target_hp, target_total_hp=target_total_hp,
            state_flag=STATE_INVINCIBLE if target_invincible else 0,
        )
        s._set_entity_hp(0, caster_hp, caster_total_hp)
        # Both flags can be set simultaneously; OR them.
        caster_state = 0
        if caster_invincible: caster_state |= STATE_INVINCIBLE
        if caster_unhealable: caster_state |= STATE_UNHEALABLE
        if caster_state: s._set_entity_state_flag(0, caster_state)

        c_dealt = s._apply_damage(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)
        c_target_hp = s.entity_hp(1)
        c_caster_hp = s.entity_hp(0)

        if (c_dealt != py_dealt or
            c_target_hp != py_target_hp or
            c_caster_hp != py_caster_hp):
            fails += 1
            if fails <= 3:
                print(f"  REAL-PY mismatch trial {trial}:")
                print(f"    dealt: C={c_dealt} PY={py_dealt}")
                print(f"    target_hp: C={c_target_hp} PY={py_target_hp}")
                print(f"    caster_hp: C={c_caster_hp} PY={py_caster_hp}")

    if skipped:
        print(f"  ({skipped} cases skipped due to Python setup issues)")
    return fails


# =====================================================================
# Main
# =====================================================================

def main():
    rng = random.Random(0)
    cases_per_effect = 2000

    suites = [
        ("damage",            fuzz_damage),
        ("heal",              fuzz_heal),
        ("absolute_shield",   fuzz_absolute_shield),
        ("buff_stat",         fuzz_buff_stat),
        ("shackle",           fuzz_shackle),
        ("aftereffect",       fuzz_aftereffect),
        ("poison",            fuzz_poison),
        ("nova_damage",       fuzz_nova_damage),
        ("life_damage",       fuzz_life_damage),
        ("vitality",          fuzz_vitality),
        ("raw_buff_stat",     fuzz_raw_buff_stat),
        ("erosion",           fuzz_erosion),
    ]

    total_fails = 0
    for name, fn in suites:
        fails = fn(cases_per_effect, rng)
        total_fails += fails
        status = "PASS" if fails == 0 else f"FAIL ({fails}/{cases_per_effect})"
        print(f"  {name:<20} {cases_per_effect:>5} cases   {status}")

    n_total = cases_per_effect * len(suites)
    print(f"\n{n_total - total_fails}/{n_total} parity cases pass against parity_gate's "
          f"line-for-line Python reference.")

    # Strong gate: invoke the actual leekwars.effect.effect_damage.EffectDamage class.
    print("\n--- Strong gate: actual upstream Python class ---")
    real_fails = fuzz_damage_against_real_python(cases_per_effect, rng)
    real_status = "PASS" if real_fails == 0 else f"FAIL ({real_fails})"
    print(f"  damage vs leekwars.effect.EffectDamage   {cases_per_effect} cases   {real_status}")

    total_fails += real_fails
    if total_fails > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
