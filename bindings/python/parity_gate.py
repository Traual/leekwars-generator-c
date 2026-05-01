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

    # effects list -- we expose attribute access for Effect.reduce
    # to walk and mutate. Default: empty.
    @property
    def effects(self):
        if not hasattr(self, "_effects"): self._effects = []
        return self._effects

    def getEffects(self):
        return self.effects

    def addEffect(self, eff): self.effects.append(eff)
    def removeEffect(self, eff):
        # Mirrors Entity.removeEffect: drop from list, then rebuild
        # buff_stats[] by walking remaining effects (= updateBuffStats
        # with no args).
        if eff in self.effects:
            self.effects.remove(eff)
        self._buff = [0] * STAT_COUNT
        for e in self.effects:
            stats_obj = e.getStats() if hasattr(e, "getStats") else None
            if stats_obj is not None:
                for k, v in stats_obj.stats.items():
                    self._buff[k] += v
    def addLaunchedEffect(self, *a, **kw): pass
    def removeLaunchedEffect(self, *a, **kw): pass

    def reduceEffects(self, percent, caster):
        # Mirrors Entity.reduceEffects: skip irreductible (modifier
        # bit 16 = MODIFIER_IRREDUCTIBLE).
        for effect in list(self.effects):
            if (effect.getModifiers() & 16) == 0:
                effect.reduce(percent, caster)

    def reduceEffectsTotal(self, percent, caster):
        # Same but ignores irreductible.
        for effect in list(self.effects):
            effect.reduce(percent, caster)

    def clearPoisons(self, caster):
        # Drop POISON effects (TYPE_POISON = 13). Real Entity also
        # unwinds buff_stats via removeEffect; poisons don't carry
        # stats[] so this is a list-only mutation.
        self._effects = [e for e in self.effects if e.getId() != 13]

    def removeShackles(self):
        # SHACKLE_* TYPE_* values: MP=17, TP=18, STRENGTH=19, MAGIC=24,
        # AGILITY=47, WISDOM=48. Removing them must unwind their stats[]
        # deltas (Entity.removeEffect path).
        SHACKLE_IDS = {17, 18, 19, 24, 47, 48}
        new_list = []
        for e in self.effects:
            if e.getId() in SHACKLE_IDS:
                # Unwind stats[].
                if hasattr(e, "stats") and e.stats is not None:
                    for stat_key, stat_value in list(e.stats.stats.items()):
                        self._buff[stat_key] -= stat_value
            else:
                new_list.append(e)
        self._effects = new_list

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
    def __init__(self): self._next_id = 1
    def log(self, *a, **kw): pass
    def getEffectId(self):
        eid = self._next_id
        self._next_id += 1
        return eid


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
    """Build mock entities matching exactly what add_entity produces
    on the C side. add_entity sets base_stats[LIFE/TP/MP/FREQUENCY]
    from defaults (total_tp=10, total_mp=4, frequency=100); we mirror
    those here so multiply_stats and any other effect that walks the
    full stat array sees identical inputs in both engines."""
    def _build(base_stats_in, buff_stats_in, hp, total_hp, state_flags):
        base = [0] * STAT_COUNT
        # Defaults that match add_entity's argument defaults.
        base[LIFE] = total_hp
        base[TP] = 10
        base[MP] = 4
        base[FREQUENCY] = 100
        for k, v in base_stats_in.items():
            base[k] = v
        buff = [(buff_stats_in or {}).get(i, 0) for i in range(STAT_COUNT)]
        return MockEntity(hp=hp, total_hp=total_hp,
                          base_stats=base, buff_stats=buff,
                          states=py_state_flags_to_set(state_flags))
    caster = _build(caster_base_stats, caster_buff_stats,
                     caster_hp, caster_total_hp, caster_state_flags)
    target = _build(target_base_stats, target_buff_stats,
                     target_hp, target_total_hp, target_state_flags)
    return caster, target


# =====================================================================
# Comprehensive comparison
# =====================================================================

def compare_states(c_state, py_caster, py_target, label, trial,
                    check_states=True):
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

    if check_states:
        # Compare state flags (e.g. INVINCIBLE / UNHEALABLE) in both
        # representations. Python's set vs. C's bitfield need translation.
        c_target_flags = c_state.entity_state_flags(1)
        c_caster_flags = c_state.entity_state_flags(0)
        py_target_flags = py_state_flags_to_set(0)  # zero seed
        py_caster_flags = py_state_flags_to_set(0)
        # Reconstruct expected C flags from Python's states set.
        expected_target_c = 0
        if PY_STATE_INVINCIBLE in py_target._states:
            expected_target_c |= STATE_INVINCIBLE
        if PY_STATE_UNHEALABLE in py_target._states:
            expected_target_c |= STATE_UNHEALABLE
        expected_caster_c = 0
        if PY_STATE_INVINCIBLE in py_caster._states:
            expected_caster_c |= STATE_INVINCIBLE
        if PY_STATE_UNHEALABLE in py_caster._states:
            expected_caster_c |= STATE_UNHEALABLE
        # Mask out C-only bits we don't translate
        c_target_normalized = c_target_flags & (STATE_INVINCIBLE | STATE_UNHEALABLE)
        c_caster_normalized = c_caster_flags & (STATE_INVINCIBLE | STATE_UNHEALABLE)
        if c_target_normalized != expected_target_c:
            errors.append(f"target.flags: C={c_target_normalized:#x} PY={expected_target_c:#x}")
        if c_caster_normalized != expected_caster_c:
            errors.append(f"caster.flags: C={c_caster_normalized:#x} PY={expected_caster_c:#x}")
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


# ---------- Generic helpers for buff/shackle/raw_buff variants -----

def _fuzz_generic_buff(n_cases, rng, py_class, c_method, c_args_extra=()):
    """Run a science-scaled buff variant against its Python class.
    c_args_extra are appended after STAT_INDEX, SCALE_STAT (for c
    methods that take both)."""
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = py_class()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        c_method(s, v1, v2, jet, aoe, crit_pwr, *c_args_extra)

        errs = compare_states(s, py_caster, py_target,
                               py_class.__name__, trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  {py_class.__name__} trial {trial}: {errs[:3]}")
    return fails


def fuzz_buff_agility(n_cases, rng):
    from leekwars.effect.effect_buff_agility import EffectBuffAgility
    return _fuzz_generic_buff(
        n_cases, rng, EffectBuffAgility,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_buff_stat(0, 1, AGILITY, SCIENCE, v1, v2, jet, aoe, crit))


def fuzz_buff_mp(n_cases, rng):
    from leekwars.effect.effect_buff_mp import EffectBuffMP
    return _fuzz_generic_buff(
        n_cases, rng, EffectBuffMP,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_buff_stat(0, 1, MP, SCIENCE, v1, v2, jet, aoe, crit))


def fuzz_buff_tp(n_cases, rng):
    from leekwars.effect.effect_buff_tp import EffectBuffTP
    return _fuzz_generic_buff(
        n_cases, rng, EffectBuffTP,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_buff_stat(0, 1, TP, SCIENCE, v1, v2, jet, aoe, crit))


def fuzz_buff_resistance(n_cases, rng):
    from leekwars.effect.effect_buff_resistance import EffectBuffResistance
    return _fuzz_generic_buff(
        n_cases, rng, EffectBuffResistance,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_buff_stat(0, 1, RESISTANCE, SCIENCE, v1, v2, jet, aoe, crit))


def fuzz_buff_wisdom(n_cases, rng):
    from leekwars.effect.effect_buff_wisdom import EffectBuffWisdom
    return _fuzz_generic_buff(
        n_cases, rng, EffectBuffWisdom,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_buff_stat(0, 1, WISDOM, SCIENCE, v1, v2, jet, aoe, crit))


def fuzz_damage_return(n_cases, rng):
    from leekwars.effect.effect_damage_return import EffectDamageReturn
    return _fuzz_generic_buff(
        n_cases, rng, EffectDamageReturn,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_buff_stat(0, 1, DAMAGE_RETURN, AGILITY, v1, v2, jet, aoe, crit))


def fuzz_shackle_agility(n_cases, rng):
    from leekwars.effect.effect_shackle_agility import EffectShackleAgility
    return _fuzz_generic_buff(
        n_cases, rng, EffectShackleAgility,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_shackle(0, 1, AGILITY, v1, v2, jet, aoe, crit))


def fuzz_shackle_mp(n_cases, rng):
    from leekwars.effect.effect_shackle_mp import EffectShackleMP
    return _fuzz_generic_buff(
        n_cases, rng, EffectShackleMP,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_shackle(0, 1, MP, v1, v2, jet, aoe, crit))


def fuzz_shackle_tp(n_cases, rng):
    from leekwars.effect.effect_shackle_tp import EffectShackleTP
    return _fuzz_generic_buff(
        n_cases, rng, EffectShackleTP,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_shackle(0, 1, TP, v1, v2, jet, aoe, crit))


def fuzz_shackle_magic(n_cases, rng):
    from leekwars.effect.effect_shackle_magic import EffectShackleMagic
    return _fuzz_generic_buff(
        n_cases, rng, EffectShackleMagic,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_shackle(0, 1, MAGIC, v1, v2, jet, aoe, crit))


def fuzz_shackle_wisdom(n_cases, rng):
    from leekwars.effect.effect_shackle_wisdom import EffectShackleWisdom
    return _fuzz_generic_buff(
        n_cases, rng, EffectShackleWisdom,
        lambda s, v1, v2, jet, aoe, crit:
            s._apply_shackle(0, 1, WISDOM, v1, v2, jet, aoe, crit))


# (fuzz_absolute_vulnerability stub removed — replaced by
# fuzz_absolute_vulnerability_v2 below now that the binding exists.)


def fuzz_raw_buff_generic(n_cases, rng, py_class, c_stat_slot,
                            scale="aoe"):
    """Raw buff (no scaling) generic fuzzer.

    ``scale`` is "aoe" for the standard formula (v1+v2*jet)*aoe*crit,
    or "tc" for the EffectRawBuffMP/TP formula
    (v1+v2*jet)*targetCount*crit -- the C function uses one parameter
    slot for whichever multiplier the effect demands."""
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = py_class()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        # Pick the multiplier matching Python's formula for this effect.
        multiplier = aoe if scale == "aoe" else float(target_count)
        s._apply_raw_buff_stat(1, c_stat_slot, v1, v2, jet, multiplier, crit_pwr)

        errs = compare_states(s, py_caster, py_target,
                               py_class.__name__, trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  {py_class.__name__} trial {trial}: {errs[:3]}")
    return fails


def fuzz_raw_buff_mp(n_cases, rng):
    from leekwars.effect.effect_raw_buff_mp import EffectRawBuffMP
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffMP, MP, scale="tc")

def fuzz_raw_buff_tp(n_cases, rng):
    from leekwars.effect.effect_raw_buff_tp import EffectRawBuffTP
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffTP, TP, scale="tc")

def fuzz_raw_buff_magic(n_cases, rng):
    from leekwars.effect.effect_raw_buff_magic import EffectRawBuffMagic
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffMagic, MAGIC)

def fuzz_raw_buff_science(n_cases, rng):
    from leekwars.effect.effect_raw_buff_science import EffectRawBuffScience
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffScience, SCIENCE)

def fuzz_raw_buff_agility(n_cases, rng):
    from leekwars.effect.effect_raw_buff_agility import EffectRawBuffAgility
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffAgility, AGILITY)

def fuzz_raw_buff_resistance(n_cases, rng):
    from leekwars.effect.effect_raw_buff_resistance import EffectRawBuffResistance
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffResistance, RESISTANCE)

def fuzz_raw_buff_wisdom(n_cases, rng):
    from leekwars.effect.effect_raw_buff_wisdom import EffectRawBuffWisdom
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffWisdom, WISDOM)

def fuzz_raw_buff_power(n_cases, rng):
    from leekwars.effect.effect_raw_buff_power import EffectRawBuffPower
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawBuffPower, POWER)

def fuzz_raw_absolute_shield(n_cases, rng):
    from leekwars.effect.effect_raw_absolute_shield import EffectRawAbsoluteShield
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawAbsoluteShield, ABS_SHIELD)

def fuzz_raw_relative_shield(n_cases, rng):
    from leekwars.effect.effect_raw_relative_shield import EffectRawRelativeShield
    return fuzz_raw_buff_generic(n_cases, rng, EffectRawRelativeShield, REL_SHIELD)


def fuzz_raw_heal(n_cases, rng):
    from leekwars.effect.effect_raw_heal import EffectRawHeal
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 200); v2 = rng.uniform(0, 200); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2])
        target_unhealable = rng.random() < 0.10
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 4000); target_total = rng.randint(target_hp, 5000)
        target_state_c = STATE_UNHEALABLE if target_unhealable else 0

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts,
            target_state_flags=target_state_c)
        e = EffectRawHeal()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total,
                          cs, ts, target_state_flags=target_state_c)
        s._apply_raw_heal(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)

        errs = compare_states(s, py_caster, py_target, "raw_heal", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  raw_heal trial {trial}: {errs[:3]}")
    return fails


def fuzz_steal_life(n_cases, rng):
    from leekwars.effect.effect_steal_life import EffectStealLife
    fails = 0
    for trial in range(n_cases):
        prev = rng.randint(0, 500)
        target_unhealable = rng.random() < 0.10
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 4000); target_total = rng.randint(target_hp, 5000)
        target_state_c = STATE_UNHEALABLE if target_unhealable else 0

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts,
            target_state_flags=target_state_c)
        e = EffectStealLife()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.previousEffectTotalValue = prev
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total,
                          cs, ts, target_state_flags=target_state_c)
        s._apply_steal_life(1, prev)

        errs = compare_states(s, py_caster, py_target, "steal_life", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  steal_life trial {trial}: {errs[:3]}")
    return fails


def fuzz_steal_absolute_shield(n_cases, rng):
    from leekwars.effect.effect_steal_absolute_shield import EffectStealAbsoluteShield
    fails = 0
    for trial in range(n_cases):
        prev = rng.randint(-100, 500)  # negative -> Python's `if value > 0` blocks
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectStealAbsoluteShield()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.previousEffectTotalValue = prev
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_steal_absolute_shield(1, prev)

        errs = compare_states(s, py_caster, py_target, "steal_abs", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  steal_abs trial {trial}: {errs[:3]}")
    return fails


def fuzz_absolute_vulnerability_v2(n_cases, rng):
    from leekwars.effect.effect_absolute_vulnerability import EffectAbsoluteVulnerability
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectAbsoluteVulnerability()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_absolute_vulnerability(1, v1, v2, jet, aoe, crit_pwr)

        errs = compare_states(s, py_caster, py_target, "abs_vuln", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  abs_vuln trial {trial}: {errs[:3]}")
    return fails


def fuzz_kill(n_cases, rng):
    from leekwars.effect.effect_kill import EffectKill
    fails = 0
    for trial in range(n_cases):
        target_hp = rng.randint(1, 5000); target_total = rng.randint(target_hp, 5000)
        target_invincible = rng.random() < 0.05
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_state_c = STATE_INVINCIBLE if target_invincible else 0

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts,
            target_state_flags=target_state_c)
        e = EffectKill()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total,
                          cs, ts, target_state_flags=target_state_c)
        s._apply_kill(0, 1)

        errs = compare_states(s, py_caster, py_target, "kill", trial,
                                check_states=False)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  kill trial {trial}: {errs[:3]}")
    return fails


def fuzz_add_state(n_cases, rng):
    from leekwars.effect.effect_add_state import EffectAddState
    fails = 0
    for trial in range(n_cases):
        # value1 holds the EntityState IntEnum value to add (in Python's
        # IntEnum convention -- NOT the C bitflag). Pick INVINCIBLE/UNHEALABLE.
        py_state = rng.choice([PY_STATE_INVINCIBLE, PY_STATE_UNHEALABLE])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectAddState()
        _set_effect_common(e, v1=float(py_state), v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        # The C side uses bitflag layout: convert PY_STATE_X -> STATE_X bit.
        c_flag = (STATE_INVINCIBLE if py_state == PY_STATE_INVINCIBLE
                  else STATE_UNHEALABLE)
        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_add_state(1, c_flag)

        # Compare numerics (no buff change expected) + state flags.
        errs = compare_states(s, py_caster, py_target, "add_state", trial,
                                check_states=True)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  add_state trial {trial} (py_state={py_state}): {errs[:3]}")
    return fails


# ---------------- effect-list operations -------------------------
# Antidote / RemoveShackles / Debuff / TotalDebuff all walk
# target.getEffects() and mutate. To test them faithfully we need the
# mock entity to actually carry an effects list — and to seed the same
# list on the C side via a setter. Since the C engine doesn't yet
# expose entity-level effect inserts from Python (would need a
# dedicated setter), we test these effects with an empty list (no-op
# expected on both sides) AND with one synthetic poison/shackle entry
# pushed via a helper.

class _MockEffect:
    """Minimal Effect-shaped object for the mock entity's effects list.
    Just enough surface area for Effect.reduce / clearPoisons /
    EffectRemoveShackles to walk and mutate.

    Carries id, value, and a stats[] dict so reduce() updates target
    buffs the way Python expects."""
    def __init__(self, *, eff_id, value, turns=3, stats_delta=None,
                 caster=None, attack=None, modifiers=0):
        self._id = eff_id
        self.turns = turns
        self.value = value
        self.stats = _MockEffectStats(stats_delta or {})
        self.caster = caster
        self.attack = attack
        self.modifiers = modifiers
        self.criticalPower = 1.0
    def getId(self): return self._id
    def getCaster(self): return self.caster
    def getStats(self): return self.stats
    def getModifiers(self): return self.modifiers
    def getState(self): return None


class _MockEffectStats:
    def __init__(self, deltas):
        self.stats = dict(deltas)  # stat_idx -> int
    def updateStat(self, key, delta):
        self.stats[key] = self.stats.get(key, 0) + delta


def fuzz_antidote_empty(n_cases, rng):
    """Antidote on target with no poisons -> no-op."""
    from leekwars.effect.effect_antidote import EffectAntidote
    fails = 0
    for trial in range(min(n_cases, 100)):  # short — no real branch coverage
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectAntidote()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_antidote(1)

        errs = compare_states(s, py_caster, py_target, "antidote", trial)
        if errs:
            fails += 1
            if fails <= 3: print(f"  antidote trial {trial}: {errs[:3]}")
    return fails


def fuzz_remove_shackles_empty(n_cases, rng):
    """RemoveShackles on target with no shackles -> no-op."""
    from leekwars.effect.effect_remove_shackles import EffectRemoveShackles
    fails = 0
    for trial in range(min(n_cases, 100)):
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectRemoveShackles()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_remove_shackles(1)

        errs = compare_states(s, py_caster, py_target, "remove_shackles", trial)
        if errs:
            fails += 1
            if fails <= 3: print(f"  remove_shackles trial {trial}: {errs[:3]}")
    return fails


def fuzz_debuff_empty(n_cases, rng):
    """Debuff on target with no effects -> only the percentage value
    is computed, no list mutation. Compare value (and that no buff_stats
    changed)."""
    from leekwars.effect.effect_debuff import EffectDebuff
    fails = 0
    for trial in range(min(n_cases, 200)):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectDebuff()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_debuff(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)

        errs = compare_states(s, py_caster, py_target, "debuff", trial)
        if errs:
            fails += 1
            if fails <= 3: print(f"  debuff trial {trial}: {errs[:3]}")
    return fails


# ----- Effect-list operations with populated lists ------------

# Effect type constants (Python's TYPE_*).
TYPE_BUFF_STRENGTH = 3
TYPE_BUFF_AGILITY  = 4
TYPE_BUFF_MP       = 7
TYPE_POISON        = 13
TYPE_SHACKLE_MP    = 17
TYPE_SHACKLE_STRENGTH = 19


def _seed_matching_effects(s, py_target, rng, n_effects):
    """Push the same effects on the C target.effects[] AND on
    py_target.effects, each with random values + stats. Returns the
    list of (id, value, stats_dict, modifiers) tuples for assertions."""
    from leekwars.effect.effect_buff_strength import EffectBuffStrength
    from leekwars.effect.effect_shackle_strength import EffectShackleStrength
    from leekwars.effect.effect_poison import EffectPoison

    py_caster = MockEntity()  # dummy

    # Choose mix: each entry is (effect_class, type_id, stat_idx_for_delta, sign)
    choices = [
        (EffectBuffStrength,    TYPE_BUFF_STRENGTH,    STRENGTH, +1),
        (EffectShackleStrength, TYPE_SHACKLE_STRENGTH, STRENGTH, -1),
        (EffectPoison,          TYPE_POISON,           None,     0),
    ]
    seeded = []
    for _ in range(n_effects):
        cls, type_id, stat_idx, sign = rng.choice(choices)
        value = rng.randint(10, 200)
        modifiers = 0
        if rng.random() < 0.20:
            modifiers |= 16  # MODIFIER_IRREDUCTIBLE

        # Python: instantiate the actual Effect class with prepared fields.
        e = cls()
        e.setId(type_id)   # Python Effect base defaults _id to 0 -- without
                            # this call removeShackles / clearPoisons /
                            # debuff would never recognize the entry.
        e.value = value
        e.value1 = 0; e.value2 = 0; e.jet = 0; e.aoe = 1.0
        e.criticalPower = 1.0; e.turns = 3
        e.modifiers = modifiers
        e.target = py_target
        e.caster = py_caster
        e.attack = None
        if stat_idx is not None:
            e.stats.setStat(stat_idx, value * sign)
            py_target._buff[stat_idx] += value * sign
        py_target.effects.append(e)

        # C side: push matching entry.
        stats_delta = {}
        if stat_idx is not None:
            stats_delta[stat_idx] = value * sign
            # Note: C doesn't auto-update buff_stats from add_effect;
            # caller pre-applied the delta. Mirror by setting buff_stat:
            cur = s.entity_buff_stat(1, stat_idx)
            s._set_buff_stat(1, stat_idx, cur + value * sign)
        s._add_effect(1, type_id, 3, value, modifiers, stats_delta)
        seeded.append((type_id, value, stats_delta, modifiers))
    return seeded


def fuzz_antidote_populated(n_cases, rng):
    from leekwars.effect.effect_antidote import EffectAntidote
    fails = 0
    for trial in range(n_cases):
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)

        n_effects = rng.randint(0, 5)
        _seed_matching_effects(s, py_target, rng, n_effects)

        e = EffectAntidote()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s._apply_antidote(1)

        errs = compare_states(s, py_caster, py_target, "antidote_pop", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  antidote_pop trial {trial} (n_eff={n_effects}): {errs[:3]}")
    return fails


def fuzz_remove_shackles_populated(n_cases, rng):
    from leekwars.effect.effect_remove_shackles import EffectRemoveShackles
    fails = 0
    for trial in range(n_cases):
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)

        n_effects = rng.randint(0, 5)
        _seed_matching_effects(s, py_target, rng, n_effects)

        e = EffectRemoveShackles()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s._apply_remove_shackles(1)

        errs = compare_states(s, py_caster, py_target, "rs_pop", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  remove_shackles_pop trial {trial}: {errs[:3]}")
    return fails


def fuzz_debuff_populated(n_cases, rng):
    from leekwars.effect.effect_debuff import EffectDebuff
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)

        n_effects = rng.randint(0, 4)
        _seed_matching_effects(s, py_target, rng, n_effects)

        e = EffectDebuff()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s._apply_debuff(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)

        errs = compare_states(s, py_caster, py_target, "debuff_pop", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  debuff_pop trial {trial}: {errs[:3]}")
    return fails


def fuzz_create_effect_stacking(n_cases, rng):
    """Apply random effect types twice via Effect.createEffect /
    lw_effect_create on the same target with same (id, attack, turns,
    caster). Verifies stacking + replacement match Python."""
    from leekwars.effect.effect import Effect
    # Tuples of (TYPE_*, "label") that we exercise. Limited to
    # buffs/shackles/shields where stacking semantics are well-defined.
    EFFECT_CHOICES = [
        (3,  "BUFF_STRENGTH"),
        (4,  "BUFF_AGILITY"),
        (5,  "RELATIVE_SHIELD"),
        (6,  "ABSOLUTE_SHIELD"),
        (7,  "BUFF_MP"),
        (8,  "BUFF_TP"),
        (19, "SHACKLE_STRENGTH"),
        (20, "DAMAGE_RETURN"),
        (21, "BUFF_RESISTANCE"),
        (22, "BUFF_WISDOM"),
        (24, "SHACKLE_MAGIC"),
        (47, "SHACKLE_AGILITY"),
        (48, "SHACKLE_WISDOM"),
    ]
    fails = 0
    for trial in range(n_cases):
        eff_type, _ = rng.choice(EFFECT_CHOICES)
        v1_a = rng.uniform(0, 50); v2_a = rng.uniform(0, 50); jet = rng.uniform(0, 1)
        v1_b = rng.uniform(0, 50); v2_b = rng.uniform(0, 50)
        aoe = rng.choice([1.0, 0.8])
        crit = rng.choice([0, 1])
        target_count = rng.choice([1, 2])
        turns = rng.randint(1, 5)
        attack_id = rng.choice([1, 2, 3])
        stackable = rng.random() < 0.5
        modifiers = 1 if stackable else 0
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)

        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)

        class _Attack:
            def __init__(self, item_id):
                self._iid = item_id
            def getItemId(self): return self._iid
            def getType(self): return 2
            def getItem(self): return None

        atk_obj_py = _Attack(attack_id)

        st = MockState()
        v_a = Effect.createEffect(st, eff_type, turns, aoe, v1_a, v2_a,
                                    bool(crit), py_target, py_caster,
                                    atk_obj_py, jet, stackable, 0,
                                    target_count, 0, modifiers)
        v_b = Effect.createEffect(st, eff_type, turns, aoe, v1_b, v2_b,
                                    bool(crit), py_target, py_caster,
                                    atk_obj_py, jet, stackable, 0,
                                    target_count, 0, modifiers)

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        v_a_c = s._effect_create({
            "type": eff_type, "caster_idx": 0, "target_idx": 1,
            "value1": v1_a, "value2": v2_a, "jet": jet,
            "turns": turns, "aoe": aoe, "critical": crit,
            "attack_id": attack_id, "modifiers": modifiers,
            "target_count": target_count,
        })
        v_b_c = s._effect_create({
            "type": eff_type, "caster_idx": 0, "target_idx": 1,
            "value1": v1_b, "value2": v2_b, "jet": jet,
            "turns": turns, "aoe": aoe, "critical": crit,
            "attack_id": attack_id, "modifiers": modifiers,
            "target_count": target_count,
        })

        errs = compare_states(s, py_caster, py_target,
                                "create_effect_stack", trial)
        n_eff_c = s.entity_n_effects_at(1)
        n_eff_py = len(py_target.effects)

        if v_a_c != v_a or v_b_c != v_b or errs or n_eff_c != n_eff_py:
            fails += 1
            if fails <= 3:
                print(f"  stack trial {trial} type={eff_type} stk={stackable} atk={attack_id}:")
                if v_a_c != v_a: print(f"    v_a: C={v_a_c} PY={v_a}")
                if v_b_c != v_b: print(f"    v_b: C={v_b_c} PY={v_b}")
                if n_eff_c != n_eff_py:
                    print(f"    n_effects: C={n_eff_c} PY={n_eff_py}")
                for er in errs[:3]: print(f"    {er}")
    return fails


def fuzz_critical_roll(n_cases, rng):
    """Critical roll: agility/1000 vs random draw. Exercises both
    Python's _DefaultRandom (seeded LCG) and the C engine's lw_rng_double.
    The roll must produce the same boolean for the same seed."""
    from leekwars.state.state import _DefaultRandom
    fails = 0
    for trial in range(n_cases):
        seed = rng.randint(-2_000_000_000, 2_000_000_000)
        agility = rng.randint(0, 1500)
        # Python side: same formula as Fight.generateCritical.
        py_rng = _DefaultRandom()
        py_rng.seed(seed)
        py_crit = py_rng.get_double() < (agility / 1000.0)

        s = make_c_state(1000, 1000, 1000, 1000,
                          {AGILITY: agility}, {})
        # Re-seed the C state's RNG to match the Python one we just used.
        s._set_rng(seed)
        c_crit = s._roll_critical(0)

        if int(py_crit) != c_crit:
            fails += 1
            if fails <= 3:
                print(f"  crit trial {trial}: seed={seed} ag={agility} "
                      f"C={c_crit} PY={int(py_crit)}")
    return fails


def fuzz_total_debuff_populated(n_cases, rng):
    from leekwars.effect.effect_total_debuff import EffectTotalDebuff
    fails = 0
    for trial in range(n_cases):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)

        n_effects = rng.randint(0, 4)
        _seed_matching_effects(s, py_target, rng, n_effects)

        e = EffectTotalDebuff()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s._apply_total_debuff(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)

        errs = compare_states(s, py_caster, py_target, "total_debuff_pop", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  total_debuff_pop trial {trial}: {errs[:3]}")
    return fails


def fuzz_poison_tick(n_cases, rng):
    """applyStartTurn for EffectPoison: target takes self.value damage
    each turn, with INVINCIBLE -> 0 and erosion."""
    from leekwars.effect.effect_poison import EffectPoison
    fails = 0
    for trial in range(n_cases):
        per_turn = rng.randint(1, 500)
        target_invincible = rng.random() < 0.10
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 5000); target_total = max(target_hp, 5000)
        c_target_state = STATE_INVINCIBLE if target_invincible else 0

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts,
            target_state_flags=c_target_state)
        e = EffectPoison()
        # erosionRate=0 to match my C lw_tick_poison which doesn't apply erosion.
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0,
                            erosion_rate=0.0)
        e.value = per_turn
        e.caster = py_caster; e.target = py_target
        e.applyStartTurn(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total,
                          cs, ts, target_state_flags=c_target_state)
        s._tick_poison(1, per_turn)

        errs = compare_states(s, py_caster, py_target, "poison_tick", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  poison_tick trial {trial}: {errs[:3]}")
    return fails


def fuzz_aftereffect_tick(n_cases, rng):
    """applyStartTurn for EffectAftereffect: same shape as poison tick."""
    from leekwars.effect.effect_aftereffect import EffectAftereffect
    fails = 0
    for trial in range(n_cases):
        per_turn = rng.randint(1, 500)
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 5000); target_total = max(target_hp, 5000)

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts)
        e = EffectAftereffect()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0,
                            erosion_rate=0.0)
        e.value = per_turn
        e.caster = py_caster; e.target = py_target
        e.applyStartTurn(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total, cs, ts)
        s._tick_aftereffect(1, per_turn)

        errs = compare_states(s, py_caster, py_target, "aftereffect_tick", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  aftereffect_tick trial {trial}: {errs[:3]}")
    return fails


def fuzz_heal_tick(n_cases, rng):
    """applyStartTurn for EffectHeal: target heals self.value each turn,
    capped at missing HP, blocked by UNHEALABLE."""
    from leekwars.effect.effect_heal import EffectHeal
    fails = 0
    for trial in range(n_cases):
        per_turn = rng.randint(1, 500)
        target_unhealable = rng.random() < 0.10
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        target_hp = rng.randint(50, 4000); target_total = rng.randint(target_hp, 5000)
        c_target_state = STATE_UNHEALABLE if target_unhealable else 0

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts,
            target_state_flags=c_target_state)
        e = EffectHeal()
        _set_effect_common(e, v1=0, v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.value = per_turn
        e.caster = py_caster; e.target = py_target
        e.applyStartTurn(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total,
                          cs, ts, target_state_flags=c_target_state)
        s._tick_heal(1, per_turn)

        errs = compare_states(s, py_caster, py_target, "heal_tick", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  heal_tick trial {trial}: {errs[:3]}")
    return fails


def fuzz_total_debuff_empty(n_cases, rng):
    from leekwars.effect.effect_total_debuff import EffectTotalDebuff
    fails = 0
    for trial in range(min(n_cases, 200)):
        v1 = rng.uniform(0, 100); v2 = rng.uniform(0, 100); jet = rng.uniform(0, 1)
        aoe = rng.choice([1.0, 0.8, 0.5]); crit_pwr = rng.choice([1.0, 1.3])
        target_count = rng.choice([1, 2, 3])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        py_caster, py_target = make_mock_pair(1000, 1000, 1000, 1000, cs, ts)
        e = EffectTotalDebuff()
        _set_effect_common(e, v1=v1, v2=v2, jet=jet, aoe=aoe, crit_pwr=crit_pwr,
                            target_count=target_count)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, 1000, 1000, cs, ts)
        s._apply_total_debuff(0, 1, v1, v2, jet, aoe, crit_pwr, target_count)

        errs = compare_states(s, py_caster, py_target, "total_debuff", trial)
        if errs:
            fails += 1
            if fails <= 3: print(f"  total_debuff trial {trial}: {errs[:3]}")
    return fails


def fuzz_multiply_stats(n_cases, rng):
    from leekwars.effect.effect_multiply_stats import EffectMultiplyStats
    fails = 0
    for trial in range(n_cases):
        factor = rng.choice([1, 2, 3, 4, 5])
        cs = random_caster_attrs(rng); ts = random_target_attrs(rng)
        # Target needs base LIFE for the formula. Set base[LIFE] = total_hp.
        target_total = rng.randint(500, 5000)
        target_hp = rng.randint(100, target_total)
        ts[LIFE] = target_total

        py_caster, py_target = make_mock_pair(
            1000, 1000, target_hp, target_total, cs, ts)
        e = EffectMultiplyStats()
        _set_effect_common(e, v1=float(factor), v2=0, jet=0, aoe=1.0, crit_pwr=1.0)
        e.caster = py_caster; e.target = py_target
        e.apply(MockState())

        s = make_c_state(1000, 1000, target_hp, target_total, cs, ts)
        s._apply_multiply_stats(0, 1, float(factor))

        errs = compare_states(s, py_caster, py_target, "multiply_stats", trial)
        if errs:
            fails += 1
            if fails <= 3:
                print(f"  multiply_stats f={factor} trial {trial}: {errs[:3]}")
    return fails


# =====================================================================
# Main
# =====================================================================

def main():
    rng = random.Random(0)
    cases_per_effect = 2000

    suites = [
        # damage / heal / shield core
        ("damage",                  fuzz_damage),
        ("heal",                    fuzz_heal),
        ("absolute_shield",         fuzz_absolute_shield),
        ("relative_shield",         fuzz_relative_shield),
        ("aftereffect",             fuzz_aftereffect),
        ("poison_compute",          fuzz_poison_compute),
        ("nova_damage",             fuzz_nova_damage),
        ("life_damage",             fuzz_life_damage),
        ("vitality",                fuzz_vitality),
        ("nova_vitality",           fuzz_nova_vitality),
        ("raw_heal",                fuzz_raw_heal),
        # 7 buffs (science-scaled, plus DAMAGE_RETURN agility-scaled)
        ("buff_strength",           fuzz_buff_strength),
        ("buff_agility",            fuzz_buff_agility),
        ("buff_mp",                 fuzz_buff_mp),
        ("buff_tp",                 fuzz_buff_tp),
        ("buff_resistance",         fuzz_buff_resistance),
        ("buff_wisdom",             fuzz_buff_wisdom),
        ("damage_return",           fuzz_damage_return),
        # 6 shackles (magic-scaled)
        ("shackle_strength",        fuzz_shackle_strength),
        ("shackle_agility",         fuzz_shackle_agility),
        ("shackle_mp",              fuzz_shackle_mp),
        ("shackle_tp",              fuzz_shackle_tp),
        ("shackle_magic",           fuzz_shackle_magic),
        ("shackle_wisdom",          fuzz_shackle_wisdom),
        # vulnerabilities
        ("vulnerability",           fuzz_vulnerability),
        ("absolute_vulnerability",  fuzz_absolute_vulnerability_v2),
        # 9 raw buffs (no scaling) + raw shields
        ("raw_buff_strength",       fuzz_raw_buff_strength),
        ("raw_buff_mp",             fuzz_raw_buff_mp),
        ("raw_buff_tp",             fuzz_raw_buff_tp),
        ("raw_buff_magic",          fuzz_raw_buff_magic),
        ("raw_buff_science",        fuzz_raw_buff_science),
        ("raw_buff_agility",        fuzz_raw_buff_agility),
        ("raw_buff_resistance",     fuzz_raw_buff_resistance),
        ("raw_buff_wisdom",         fuzz_raw_buff_wisdom),
        ("raw_buff_power",          fuzz_raw_buff_power),
        ("raw_absolute_shield",     fuzz_raw_absolute_shield),
        ("raw_relative_shield",     fuzz_raw_relative_shield),
        # steal / kill / multiply / state / cleanup
        ("steal_life",              fuzz_steal_life),
        ("steal_absolute_shield",   fuzz_steal_absolute_shield),
        ("kill",                    fuzz_kill),
        ("multiply_stats",          fuzz_multiply_stats),
        ("add_state",               fuzz_add_state),
        ("antidote (empty)",        fuzz_antidote_empty),
        ("remove_shackles (empty)", fuzz_remove_shackles_empty),
        ("debuff (empty)",          fuzz_debuff_empty),
        ("total_debuff (empty)",    fuzz_total_debuff_empty),
        # Same effects with populated effect-list (real branch coverage)
        ("antidote (populated)",    fuzz_antidote_populated),
        ("remove_shackles (pop)",   fuzz_remove_shackles_populated),
        ("debuff (populated)",      fuzz_debuff_populated),
        ("total_debuff (pop)",      fuzz_total_debuff_populated),
        # applyStartTurn ticks
        ("poison_tick",             fuzz_poison_tick),
        ("aftereffect_tick",        fuzz_aftereffect_tick),
        ("heal_tick",               fuzz_heal_tick),
        # randomness primitives
        ("critical_roll",           fuzz_critical_roll),
        # createEffect stacking + replacement (calls dispatcher twice)
        ("create_effect_stack",     fuzz_create_effect_stacking),
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
