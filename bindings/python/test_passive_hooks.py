"""Passive event hooks integration test.

Registers a weapon with DAMAGE_TO_STRENGTH and MOVED_TO_MP passives
on the *target* (the entity that takes damage / gets moved), fires
the relevant pipelines, then verifies the right RAW_BUFF_* effect
landed on the target's stats.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from leekwars_c._engine import (
    State, Topology, AttackSpec,
    catalog_register, catalog_clear,
)


# Effect type ids matching include/lw_effect.h
TYPE_DAMAGE                  = 1
TYPE_DAMAGE_TO_STRENGTH      = 35
TYPE_MOVED_TO_MP             = 50
TYPE_RAW_BUFF_STRENGTH       = 38
TYPE_RAW_BUFF_MP             = 31

# Stat indices.
STRENGTH = 3
MP = 2


def build_15x15():
    cells = []; neighbors = []
    W = H = 15
    for y in range(H):
        for x in range(W):
            cid = y * W + x
            cells.append((cid, x, y, True))
            s = (y + 1) * W + x if y + 1 < H else -1
            w = y * W + (x - 1) if x - 1 >= 0 else -1
            n = (y - 1) * W + x if y - 1 >= 0 else -1
            e = y * W + (x + 1) if x + 1 < W else -1
            neighbors.append((s, w, n, e))
    return Topology.from_grid(W, H, cells, neighbors), W, H


def test_damage_to_strength():
    """Target wields a weapon with a DAMAGE_TO_STRENGTH passive (50%).
    When the target takes 100 damage, the passive fires and adds
    50 STRENGTH to the target's buff_stats."""
    topo, W, H = build_15x15()
    catalog_clear()

    # Caster's weapon: plain damage.
    plain = AttackSpec(item_id=1, attack_type=1, min_range=1, max_range=12,
                        launch_type=1, area=1, needs_los=1, tp_cost=4)
    plain.add_effect(type=TYPE_DAMAGE, value1=100, value2=0,
                      turns=0, targets_filter=1)
    catalog_register(plain)

    # Target's weapon: carries the passive. The target wields this
    # weapon; the passive fires when the target takes direct damage.
    passive_weapon = AttackSpec(item_id=2, attack_type=1, min_range=1,
                                 max_range=12, launch_type=1, area=1,
                                 needs_los=1, tp_cost=4)
    passive_weapon.add_passive(type=TYPE_DAMAGE_TO_STRENGTH,
                                value1=50.0,  # 50% of input damage
                                turns=3)
    catalog_register(passive_weapon)

    s = State()
    s.set_topology(topo)
    s.add_entity(fid=1, team_id=0, cell_id=7 * W + 4,
                 total_tp=8, total_mp=4, hp=2000, total_hp=2000,
                 weapons=[1], chips=[],
                 strength=0, agility=0, frequency=100, level=100)
    s.add_entity(fid=2, team_id=1, cell_id=7 * W + 10,
                 total_tp=8, total_mp=4, hp=2000, total_hp=2000,
                 weapons=[2], chips=[],
                 strength=0, agility=0, frequency=99, level=100)
    s.compute_start_order()
    s._set_rng(42)

    target_str_before = s.entity_buff_stat(1, STRENGTH)

    # Caster fires weapon 1 at target's cell. Target takes ~100 damage.
    s.apply_action_use_weapon(0, weapon_id=1,
                                target_cell_id=s.entity_cell(1))

    target_str_after = s.entity_buff_stat(1, STRENGTH)

    # The passive should have added 50% of dealt damage to target's
    # STRENGTH buff_stats. With v1=100, no jet/agi mods, dealt should
    # be ~100 (rng-dependent), so str gain ~50.
    str_gain = target_str_after - target_str_before
    target_hp_lost = 2000 - s.entity_hp(1)

    expected_gain = round(target_hp_lost * 0.5)

    if str_gain == expected_gain and str_gain > 0:
        print(f"  damage_to_strength: dealt={target_hp_lost} "
              f"str_gain={str_gain} expected={expected_gain}  PASS")
        return True
    print(f"  damage_to_strength: dealt={target_hp_lost} "
          f"str_gain={str_gain} expected={expected_gain}  FAIL")
    return False


def test_moved_to_mp():
    """Target wields a weapon with MOVED_TO_MP passive. Caster fires
    a Push attack -- target moves -> on_moved fires -> +MP buff."""
    topo, W, H = build_15x15()
    catalog_clear()

    # Caster's chip: Push along a laser line. The line passes through
    # the target's cell (catches it in the area), then the PUSH effect
    # walks the target away from the caster.
    LASER_LINE = 2
    push = AttackSpec(item_id=10, attack_type=2, min_range=1, max_range=12,
                       launch_type=1, area=LASER_LINE,
                       needs_los=0, tp_cost=4)
    push.add_effect(type=51, value1=0, value2=0, turns=0,
                     targets_filter=1)
    catalog_register(push)

    # Target's weapon with MOVED_TO_MP passive: +5 MP per move.
    passive_weapon = AttackSpec(item_id=11, attack_type=1, min_range=1,
                                 max_range=12, launch_type=1, area=1,
                                 needs_los=1, tp_cost=4)
    passive_weapon.add_passive(type=TYPE_MOVED_TO_MP,
                                value1=5.0,
                                turns=3)
    catalog_register(passive_weapon)

    s = State()
    s.set_topology(topo)
    # Caster at (7,4), target at (7,7). Pushing target away from caster
    # along the +y axis means target moves to higher y. Push to target_cell
    # (7,12) so the path goes y=7 -> y=8 -> ...
    s.add_entity(fid=1, team_id=0, cell_id=4 * W + 7,
                 total_tp=8, total_mp=4, hp=2000, total_hp=2000,
                 weapons=[1], chips=[],
                 frequency=100, level=100)
    s.add_entity(fid=2, team_id=1, cell_id=7 * W + 7,
                 total_tp=8, total_mp=4, hp=2000, total_hp=2000,
                 weapons=[11], chips=[],
                 frequency=99, level=100)
    s.compute_start_order()
    s._set_rng(42)

    target_mp_before = s.entity_buff_stat(1, MP)
    target_cell_before = s.entity_cell(1)

    # Caster fires push chip aiming further along the line so target
    # actually moves.
    s.apply_action_use_weapon(0, weapon_id=10,
                                target_cell_id=12 * W + 7)

    target_mp_after = s.entity_buff_stat(1, MP)
    target_cell_after = s.entity_cell(1)
    moved = target_cell_after != target_cell_before

    if moved and target_mp_after - target_mp_before == 5:
        print(f"  moved_to_mp: moved={target_cell_before}->{target_cell_after} "
              f"mp_gain={target_mp_after - target_mp_before}  PASS")
        return True
    print(f"  moved_to_mp: moved={moved} cell={target_cell_before}->"
          f"{target_cell_after} mp_gain={target_mp_after - target_mp_before}  FAIL")
    return False


def main():
    print("test_passive_hooks:")
    n = ok = 0
    n += 1; ok += int(test_damage_to_strength())
    n += 1; ok += int(test_moved_to_mp())
    print(f"\n{ok}/{n} passive integration cases pass")
    if ok < n:
        sys.exit(1)


if __name__ == "__main__":
    main()
