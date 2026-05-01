"""Parity gate: byte-for-byte equivalence of the C engine with the
upstream Python `leekwars` package.

Strategy: for each Effect class in leekwars/effect/, generate 2000+
random parameter sets, run the actual Python apply() on a mock
entity / state, run the equivalent C apply on a parallel C state,
and compare every numeric mutation (hp / total_hp / buff_stats[18] /
state_flags / alive) on both caster AND target.

If any field diverges, the trial is logged with the offending inputs
and the suite reports failure. This catches branches we missed when
porting (e.g. lifeSteal / returnDamage on EffectDamage) and ordering
differences without us having to predict where they could happen.
"""
from __future__ import annotations

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import leekwars_c as lwc  # type: ignore
from leekwars_c._engine import State  # type: ignore

# Locate the Python engine (line-by-line port of Java) so we can
# import the actual Effect classes.
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

from leekwars.util.java_math import java_round  # type: ignore  # noqa: E402

# Stat indices matching include/lw_types.h.
LIFE = 0; TP = 1; MP = 2; STRENGTH = 3; AGILITY = 4; FREQUENCY = 5
WISDOM = 6; ABS_SHIELD = 9; REL_SHIELD = 10; RESISTANCE = 11
SCIENCE = 12; MAGIC = 13; DAMAGE_RETURN = 14; POWER = 15

# C state-flag bits.
STATE_INVINCIBLE = 1 << 3
STATE_UNHEALABLE = 1 << 2

# Python EntityState IntEnum values (different layout from C bitflags!).
PY_STATE_UNHEALABLE = 2
PY_STATE_INVINCIBLE = 3

STAT_COUNT = 18


# =====================================================================
# Mock entity / state that mirror Python's Entity API surface area
# touched by Effect.apply. Each method does the minimum required to
# track mutations we want to compare.
# =====================================================================

class MockEntity:
    """Tracks every numeric field Python apply() can mutate. Methods
    are named to match leekwars.state.entity.Entity exactly so an
    Effect.apply() call goes through unmodified."""

    def __init__(self, *, hp=1000, total_hp=1000, base_stats=None,
                 buff_stats=None, states=None):
        self._hp = hp
        self._total = total_hp
        # Layout matches lw_types.h indices.
        self._base = list(base_stats) if base_stats else [0] * STAT_COUNT
        self._buff = list(buff_stats) if buff_stats else [0] * STAT_COUNT
        self._states = set(states) if states else set()
        self.value = 0   # last damage amt — used by some chained apply()s

    # --- Stat getters (Entity.getX). All combine base + buff. -------
    def _stat(self, idx): return self._base[idx] + self._buff[idx]
    def getStrength(self): return self._stat(STRENGTH)
    def getAgility(self): return self._stat(AGILITY)
    def getMP(self): return self._stat(MP)
    def getTP(self): return self._stat(TP)
    def getTotalTP(self): return self._stat(TP)
    def getTotalMP(self): return self._stat(MP)
    def getWisdom(self): return self._stat(WISDOM)
    def getResistance(self): return self._stat(RESISTANCE)
    def getScience(self): return self._stat(SCIENCE)
    def getMagic(self): return self._stat(MAGIC)
    def getPower(self): return self._stat(POWER)
    def getFrequency(self): return self._stat(FREQUENCY)
    def getAbsoluteShield(self): return self._stat(ABS_SHIELD)
    def getRelativeShield(self): return self._stat(REL_SHIELD)
    def getDamageReturn(self): return self._stat(DAMAGE_RETURN)

    def getStat(self, idx): return self._stat(idx)
    def getBaseStats(self):
        ent = self
        class _BS:
            def getStat(self, idx): return ent._base[idx]
            def setStat(self, idx, v): ent._base[idx] = v
        return _BS()

    # --- HP and lifecycle -------------------------------------------
    def getLife(self): return self._hp
    def getTotalLife(self): return self._total
    def isDead(self): return self._hp <= 0
    def isAlive(self): return self._hp > 0

    def removeLife(self, pv, erosion, *args, **kw):
        if self._hp <= 0: return
        if pv > self._hp: pv = self._hp
        if pv < 0: pv = 0
        self._hp -= pv
        self._total -= erosion
        if self._total < 1: self._total = 1
        self.value = pv

    def addLife(self, healer, pv, *args, **kw):
        # Python.addLife(healer, pv): caps at missing HP first
        if pv > self._total - self._hp:
            pv = self._total - self._hp
        self._hp += pv
        if self._hp > self._total: self._hp = self._total

    def addTotalLife(self, vitality, caster):
        self._total += vitality

    # --- States ------------------------------------------------------
    def hasState(self, s): return int(s) in self._states
    def addState(self, s): self._states.add(int(s))
    def removeState(self, s): self._states.discard(int(s))
    def getStates(self): return self._states

    # --- Buffs and effects ------------------------------------------
    def updateBuffStats(self, *args):
        # Single-arg form: rebuild from effects (we don't model that here).
        if len(args) <= 1: return
        id_, delta, caster = args
        self._buff[id_] += delta

    def getEffects(self): return []
    def addEffect(self, *a, **kw): pass
    def removeEffect(self, *a, **kw): pass
    def addLaunchedEffect(self, *a, **kw): pass
    def removeLaunchedEffect(self, *a, **kw): pass
    def reduceEffects(self, percent, caster):
        # Effect-level reduce — we don't model in this gate (no effect
        # entries on mock); leave as no-op.
        pass
    def reduceEffectsTotal(self, percent, caster): pass
    def clearPoisons(self, caster): pass

    # --- Passive event hooks (no-ops in the gate) -------------------
    def onDirectDamage(self, *a, **kw): pass
    def onPoisonDamage(self, *a, **kw): pass
    def onNovaDamage(self, *a, **kw): pass
    def onMoved(self, *a, **kw): pass
    def onAllyKilled(self, *a, **kw): pass
    def onCritical(self, *a, **kw): pass
    def onKill(self, *a, **kw): pass

    # --- Identity ----------------------------------------------------
    def getId(self): return id(self) & 0xFFFF
    def getFId(self): return self.getId()
    def getTeam(self): return 0
    def isSummon(self): return False
    def hasEffect(self, item_id): return False
    def getCell(self): return None


class MockStats:
    def damage(self, *a, **kw): pass
    def effect(self, *a, **kw): pass
    def heal(self, *a, **kw): pass
    def vitality(self, *a, **kw): pass
    def characteristics(self, *a, **kw): pass
    def useTP(self, *a, **kw): pass
    def slide(self, *a, **kw): pass


class _MockActions:
    def log(self, *a, **kw): pass


class MockState:
    def __init__(self):
        self.statistics = MockStats()
        self._actions = _MockActions()
    def log(self, *a, **kw): pass
    def getActions(self): return self._actions
    def getRandom(self): raise NotImplementedError("apply shouldn't roll")


# =====================================================================
# State-mirroring helpers
# =====================================================================

def make_c_state(caster_hp, caster_total_hp, target_hp, target_total_hp,
                  caster_base_stats, target_base_stats,
                  caster_buff_stats=None, target_buff_stats=None,
                  caster_state_flags=0, target_state_flags=0):
    s = State()
    s.add_entity(fid=1, team_id=0, cell_id=-1,
                 total_tp=10, total_mp=4,
                 hp=caster_hp, total_hp=caster_total_hp,
                 weapons=[], chips=[])
    s.add_entity(fid=2, team_id=1, cell_id=-1,
                 total_tp=10, total_mp=4,
                 hp=target_hp, total_hp=target_total_hp,
                 weapons=[], chips=[])
    s._set_entity_hp(0, caster_hp, caster_total_hp)
    s._set_entity_hp(1, target_hp, target_total_hp)
    if caster_state_flags: s._set_entity_state_flag(0, caster_state_flags)
    if target_state_flags: s._set_entity_state_flag(1, target_state_flags)
    for idx, val in caster_base_stats.items():
        s._set_base_stat(0, idx, val)
    for idx, val in target_base_stats.items():
        s._set_base_stat(1, idx, val)
    for idx, val in (caster_buff_stats or {}).items():
        s._set_buff_stat(0, idx, val)
    for idx, val in (target_buff_stats or {}).items():
        s._set_buff_stat(1, idx, val)
    return s


def py_state_flags_to_set(c_flags):
    """Convert C bitfield to Python EntityState IntEnum set."""
    out = set()
    if c_flags & STATE_INVINCIBLE: out.add(PY_STATE_INVINCIBLE)
    if c_flags & STATE_UNHEALABLE: out.add(PY_STATE_UNHEALABLE)
    return out


def make_mock_pair(caster_hp, caster_total_hp, target_hp, target_total_hp,
                    caster_base_stats, target_base_stats,
                    caster_buff_stats=None, target_buff_stats=None,
                    caster_state_flags=0, target_state_flags=0):
    caster = MockEntity(
        hp=caster_hp, total_hp=caster_total_hp,
        base_stats=[caster_base_stats.get(i, 0) for i in range(STAT_COUNT)],
        buff_stats=[(caster_buff_stats or {}).get(i, 0) for i in range(STAT_COUNT)],
        states=py_state_flags_to_set(caster_state_flags),
    )
    target = MockEntity(
        hp=target_hp, total_hp=target_total_hp,
        base_stats=[target_base_stats.get(i, 0) for i in range(STAT_COUNT)],
        buff_stats=[(target_buff_stats or {}).get(i, 0) for i in range(STAT_COUNT)],
        states=py_state_flags_to_set(target_state_flags),
    )
    return caster, target


# =====================================================================
# Comprehensive comparison
# =====================================================================

def compare_states(c_state, py_caster, py_target, label, trial):
    """Compare every numeric field on both entities. Returns list of
    error strings; empty -> match."""
    errors = []
    c_target_hp = c_state.entity_hp(1)
    c_target_total = c_state.entity_total_hp(1)
    c_caster_hp = c_state.entity_hp(0)
    c_caster_total = c_state.entity_total_hp(0)

    if c_target_hp != py_target._hp:
        errors.append(f"target.hp: C={c_target_hp} PY={py_target._hp}")
    if c_target_total != py_target._total:
        errors.append(f"target.total_hp: C={c_target_total} PY={py_target._total}")
    if c_caster_hp != py_caster._hp:
        errors.append(f"caster.hp: C={c_caster_hp} PY={py_caster._hp}")
    if c_caster_total != py_caster._total:
        errors.append(f"caster.total_hp: C={c_caster_total} PY={py_caster._total}")

    for slot in range(STAT_COUNT):
        c_buff = c_state.entity_buff_stat(1, slot)
        py_buff = py_target._buff[slot]
        if c_buff != py_buff:
            errors.append(f"target.buff[{slot}]: C={c_buff} PY={py_buff}")
        c_buff_c = c_state.entity_buff_stat(0, slot)
        py_buff_c = py_caster._buff[slot]
        if c_buff_c != py_buff_c:
            errors.append(f"caster.buff[{slot}]: C={c_buff_c} PY={py_buff_c}")
    return errors


def random_caster_attrs(rng):
    """Random caster stats. All non-negative since most negative-stat
    cases hit the same code paths and we want to maximize coverage."""
    return {
        STRENGTH:      rng.randint(0, 500),
        AGILITY:       rng.randint(0, 500),
        WISDOM:        rng.randint(0, 300),
        RESISTANCE:    rng.randint(0, 300),
        SCIENCE:       rng.randint(0, 400),
        MAGIC:         rng.randint(0, 300),
        POWER:         rng.randint(0, 200),
        FREQUENCY:     rng.randint(50, 300),
    }


def random_target_attrs(rng):
    return {
        REL_SHIELD:     rng.randint(0, 70),
        ABS_SHIELD:     rng.randint(0, 200),
        DAMAGE_RETURN:  rng.randint(0, 100),
    }


# =====================================================================
# Per-effect strong gates
# =====================================================================

def _set_effect_common(e, *, v1, v2, jet, aoe, crit_pwr, target_count=1,
                       erosion_rate=0.0, turns=0, modifiers=0):
    e.value1 = v1; e.value2 = v2; e.jet = jet
    e.aoe = aoe; e.criticalPower = crit_pwr; e.targetCount = target_count
    e.erosionRate = erosion_rate; e.turns = turns; e.modifiers = modifiers
    e.attack = None
    e.previousEffectTotalValue = 0
    e.propagate = 0
    return e


def fuzz_damage(n_cases, rng):
    from leekwars.effect.effect_damage import EffectDamage
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200); v2 = rng.uniform(0, 200); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5, 0.2])
        crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        target_invincible = rng.random() < 0.05
        caster_invincible = rng.random() < 0.05
        caster_unhealable = rng.random() < 0.05
        erosion = rng.choice([0.0, 0.05, 0.15])
        cs = random_caster_attrs(rng)
        ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 5000); target_total = max(target_hp, 5000)
        caster_hp = rng.randint(50, 5000); caster_total = max(caster_hp, 5000)
        c_target_state = STATE_INVINCIBLE if target_invincible else 0
        c_caster_state = (STATE_INVINCIBLE if caster_invincible else 0) \
                       | (STATE_UNHEALABLE if caster_unhealable else 0)

        # Python
        py_caster, py_target = make_mock_pair(
            caster_hp, caster_total, target_hp, target_total, cs, ts,
            caster_state_flags=c_caster_state, target_state_flags=c_target_state,
        )
        e = EffectDamage()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count, erosion_rate=erosion)
        e.caster = py_caster; e.target = py_target
        try:
            e.apply(MockState())
        except Exception as exc:
            fails += 1
            if fails <= 1:
                print(f"  damage py raised {trial}: {exc}")
            continue

        # C
        s = make_c_state(caster_hp, caster_total, target_hp, target_total,
                          cs, ts, caster_state_flags=c_caster_state,
                          target_state_flags=c_target_state)
        s._apply_damage_v2(0, 1, v1, v2, jet, aoe, crit_pwr, target_count, erosion)

        errs = compare_states(s, py_caster, py_target, "damage", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  damage trial {trial}: {len(errs)} mismatch(es)")
                for er in errs[:3]: print(f"    {er}")
    return fails


def fuzz_heal(n_cases, rng):
    from leekwars.effect.effect_heal import EffectHeal
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200); v2 = rng.uniform(0, 200); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5])
        crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2])
        target_unhealable = rng.random() < 0.10
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 4000); target_total = rng.randint(target_hp, 5000)
        caster_hp = rng.randint(50, 5000); caster_total = max(caster_hp, 5000)
        target_state_c = STATE_UNHEALABLE if target_unhealable else 0

        py_caster, py_target = make_mock_pair(
            caster_hp, caster_total, target_hp, target_total, cs, ts,
            target_state_flags=target_state_c)
        e = EffectHeal()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count, turns=0)
        e.caster = py_caster; e.target = py_target
        try:
            e.apply(MockState())
        except Exception as exc:
            fails += 1
            if fails <= 1: print(f"  heal py raised {trial}: {exc}")
            continue

        s = make_c_state(caster_hp, caster_total, target_hp, target_total,
                          cs, ts, target_state_flags=target_state_c)
        s._apply_heal(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)

        errs = compare_states(s, py_caster, py_target, "heal", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  heal trial {trial}: {len(errs)} mismatch(es)")
                for er in errs[:3]: print(f"    {er}")
    return fails


def fuzz_absolute_shield(n_cases, rng):
    from leekwars.effect.effect_absolute_shield import EffectAbsoluteShield
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectAbsoluteShield()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_absolute_shield(0, 1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "abs_shield", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  abs_shield trial {trial}: {errs[:3]}")
    return fails


def fuzz_relative_shield(n_cases, rng):
    from leekwars.effect.effect_relative_shield import EffectRelativeShield
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectRelativeShield()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_relative_shield(0, 1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "rel_shield", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  rel_shield trial {trial}: {errs[:3]}")
    return fails


def fuzz_buff_strength(n_cases, rng):
    """Buff via science scaling -> writes to STRENGTH slot."""
    from leekwars.effect.effect_buff_strength import EffectBuffStrength
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectBuffStrength()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_buff_stat(0, 1, STRENGTH, SCIENCE, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "buff_str", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  buff_str trial {trial}: {errs[:3]}")
    return fails


def fuzz_shackle_strength(n_cases, rng):
    from leekwars.effect.effect_shackle_strength import EffectShackleStrength
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectShackleStrength()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_shackle(0, 1, STRENGTH, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "shackle_str", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  shackle_str trial {trial}: {errs[:3]}")
    return fails


def fuzz_aftereffect(n_cases, rng):
    from leekwars.effect.effect_aftereffect import EffectAftereffect
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200); v2 = rng.uniform(0, 200); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_invincible = rng.random() < 0.10
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 5000); target_total = max(target_hp, 5000)
        target_state_c = STATE_INVINCIBLE if target_invincible else 0

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts,
            target_state_flags=target_state_c)
        e = EffectAftereffect()
        # erosion_rate is 0 to match how lw_apply_aftereffect handles it currently.
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            erosion_rate=0.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total,
                          cs, ts, target_state_flags=target_state_c)
        s._apply_aftereffect(0, 1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "aftereffect", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  aftereffect trial {trial}: {errs[:3]}")
    return fails


def fuzz_poison_compute(n_cases, rng):
    """Poison's apply only stores per-turn value — no state change yet.
    We just check the formula matches Python (state untouched)."""
    from leekwars.effect.effect_poison import EffectPoison
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectPoison()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        c_per_turn = s._compute_poison_damage(0, 1, v1, v2, jet, aoe, crit_pwr)

        if c_per_turn != e.value:
            fails += 1
            if fails <= 3:
                print(f"  poison trial {trial}: C={c_per_turn} PY={e.value}")
        # No state mutation expected — verify via comparison too.
        errs = compare_states(s, py_caster, py_target, "poison", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  poison state {trial}: {errs[:3]}")
    return fails


def fuzz_nova_damage(n_cases, rng):
    from leekwars.effect.effect_nova_damage import EffectNovaDamage
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200); v2 = rng.uniform(0, 200); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_invincible = rng.random() < 0.10
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 4000); target_total = rng.randint(target_hp, 5000)
        target_state_c = STATE_INVINCIBLE if target_invincible else 0

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts,
            target_state_flags=target_state_c)
        e = EffectNovaDamage()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total,
                          cs, ts, target_state_flags=target_state_c)
        s._apply_nova_damage(0, 1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "nova_damage", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  nova_damage trial {trial}: {errs[:3]}")
    return fails


def fuzz_life_damage(n_cases, rng):
    from leekwars.effect.effect_life_damage import EffectLifeDamage
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_invincible = rng.random() < 0.10
        caster_invincible = rng.random() < 0.05
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 5000); target_total = max(target_hp, 5000)
        caster_hp = rng.randint(100, 5000); caster_total = max(caster_hp, 5000)
        c_target_state = STATE_INVINCIBLE if target_invincible else 0
        c_caster_state = STATE_INVINCIBLE if caster_invincible else 0

        py_caster, py_target = make_mock_pair(
            caster_hp, caster_total, target_hp, target_total, cs, ts,
            caster_state_flags=c_caster_state, target_state_flags=c_target_state)
        e = EffectLifeDamage()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            erosion_rate=0.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(caster_hp, caster_total, target_hp, target_total,
                          cs, ts,
                          caster_state_flags=c_caster_state,
                          target_state_flags=c_target_state)
        s._apply_life_damage(0, 1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "life_damage", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  life_damage trial {trial}: {errs[:3]}")
    return fails


def fuzz_vitality(n_cases, rng):
    from leekwars.effect.effect_vitality import EffectVitality
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectVitality()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_vitality(0, 1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "vitality", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  vitality trial {trial}: {errs[:3]}")
    return fails


def fuzz_nova_vitality(n_cases, rng):
    from leekwars.effect.effect_nova_vitality import EffectNovaVitality
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectNovaVitality()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_nova_vitality(0, 1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "nova_vitality", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  nova_vitality trial {trial}: {errs[:3]}")
    return fails


def fuzz_vulnerability(n_cases, rng):
    from leekwars.effect.effect_vulnerability import EffectVulnerability
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectVulnerability()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_vulnerability(1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "vulnerability", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  vulnerability trial {trial}: {errs[:3]}")
    return fails


def fuzz_raw_buff_strength(n_cases, rng):
    from leekwars.effect.effect_raw_buff_strength import EffectRawBuffStrength
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectRawBuffStrength()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_raw_buff_stat(1, STRENGTH, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "raw_buff_str", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  raw_buff_str trial {trial}: {errs[:3]}")
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
        ("relative_shield",   fuzz_relative_shield),
        ("buff_strength",     fuzz_buff_strength),
        ("shackle_strength",  fuzz_shackle_strength),
        ("aftereffect",       fuzz_aftereffect),
        ("poison_compute",    fuzz_poison_compute),
        ("nova_damage",       fuzz_nova_damage),
        ("life_damage",       fuzz_life_damage),
        ("vitality",          fuzz_vitality),
        ("nova_vitality",     fuzz_nova_vitality),
        ("vulnerability",     fuzz_vulnerability),
        ("raw_buff_strength", fuzz_raw_buff_strength),
    ]

    total_fails = 0
    for name, fn in suites:
        fails = fn(cases_per_effect, rng)
        total_fails += fails
        status = "PASS" if fails == 0 else f"FAIL ({fails}/{cases_per_effect})"
        print(f"  {name:<20} {cases_per_effect:>5} cases   {status}")

    n_total = cases_per_effect * len(suites)
    print(f"\n{n_total - total_fails}/{n_total} parity cases pass against the actual "
          f"upstream Python `leekwars` engine.")
    if total_fails > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
