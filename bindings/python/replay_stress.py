"""Stress-test fights for the local replay viewer.

Generates a single fight from a *named scenario* (1v1, 2v2, 4v4, 6v6,
battle royale, exotic weapon, exotic chip) and writes it to the
leek-wars-client's ``public/static/report.json`` so we can play it
through the viewer and surface client-side NPEs *before* training the
AlphaZero net.

Usage:
    python replay_stress.py --list
    python replay_stress.py --scenario 4v4_team
    python replay_stress.py --scenario all   # writes one report.json per
                                             # scenario to /tmp + prints
                                             # action counts (no viewer)

The scenario library covers:

* every team-size we care about (1v1, 2v2, 4v4, 6v6, BR-6, BR-10),
* a representative sample of weapons (line / area / sword / sniper /
  bazooka / lightninger / flame_thrower / electrisor),
* a representative sample of damage chips (point / line / aoe / poison
  / freeze / lightning / meteorite / rockfall),
* defensive chips (heal, shield, helmet, carapace, rampart),
* buff chips (rage, reflexes, fortress, wall).

Summons + resurrection are NOT covered by this script -- the binding
doesn't expose summon entities yet, so the client would need an extra
patch (TODO).
"""
from __future__ import annotations

import argparse
import os
import sys
from typing import Iterable

# Make the bindings importable regardless of CWD.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")

import leekwars_c._engine as _v2
from leekwars_c.replay import build_leek, build_report, write_report

import test_v2_summon as base   # WEAPONS / CHIPS / register_all_chips_v2

DEFAULT_REPORT = (
    "C:/Users/aurel/Desktop/Training Weights Leekwars/"
    "Leekwars-Tools/leek-wars/public/static/report.json"
)

# ---- Engine type constants (mirrors State.TYPE_* in upstream Java) ----
TYPE_SOLO = 1
TYPE_TEAM = 2
TYPE_BATTLE_ROYALE = 3


# ---- Scenario library ---------------------------------------------------
# Each scenario dict is a recipe for `run_fight(...)`:
#   - mode: 'team' or 'br'
#   - per_team: int (team mode) -- both teams get the same number
#   - n_players: int (br mode)  -- 2..10
#   - weapons:   list of weapon item_ids each leek carries
#   - chips:     list of chip   item_ids each leek carries
#   - max_turns: optional cap (default 25)
#
# When multiple weapons are listed, the AI just fires the *first* one
# repeatedly. This is a stress test of the action stream, not an AI
# benchmark -- we only care that nothing crashes the viewer.

SCENARIOS: dict[str, dict] = {
    # -- team fights, default loadout (pistol + flame) --------------------
    "1v1_pistol_flame":   dict(mode="team", per_team=1, weapons=[37],  chips=[5]),
    "2v2_pistol_flame":   dict(mode="team", per_team=2, weapons=[37],  chips=[5]),
    "4v4_pistol_flame":   dict(mode="team", per_team=4, weapons=[37],  chips=[5]),
    "6v6_pistol_flame":   dict(mode="team", per_team=6, weapons=[37],  chips=[5]),

    # -- battle royale ----------------------------------------------------
    "br_4":  dict(mode="br", n_players=4,  weapons=[37], chips=[5]),
    "br_6":  dict(mode="br", n_players=6,  weapons=[37], chips=[5]),
    "br_10": dict(mode="br", n_players=10, weapons=[37], chips=[5]),

    # -- weapon coverage (1v1 each) --------------------------------------
    "weapon_machine_gun":  dict(mode="team", per_team=1, weapons=[38],  chips=[5]),
    "weapon_shotgun":      dict(mode="team", per_team=1, weapons=[41],  chips=[5]),
    "weapon_laser":        dict(mode="team", per_team=1, weapons=[42],  chips=[5]),
    "weapon_double_gun":   dict(mode="team", per_team=1, weapons=[39],  chips=[5]),
    "weapon_destroyer":    dict(mode="team", per_team=1, weapons=[40],  chips=[5]),
    "weapon_flame_thrower": dict(mode="team", per_team=1, weapons=[46], chips=[5]),
    "weapon_grenade_launcher": dict(mode="team", per_team=1, weapons=[43], chips=[5]),
    "weapon_electrisor":   dict(mode="team", per_team=1, weapons=[44],  chips=[5]),
    "weapon_lightninger":  dict(mode="team", per_team=1, weapons=[180], chips=[5]),
    "weapon_rifle":        dict(mode="team", per_team=1, weapons=[151], chips=[5]),
    "weapon_bazooka":      dict(mode="team", per_team=1, weapons=[184], chips=[5]),
    "weapon_sword":        dict(mode="team", per_team=1, weapons=[277], chips=[5]),
    "weapon_katana":       dict(mode="team", per_team=1, weapons=[107], chips=[5]),
    "weapon_axe":          dict(mode="team", per_team=1, weapons=[109], chips=[5]),
    "weapon_neutrino":     dict(mode="team", per_team=1, weapons=[182], chips=[5]),
    "weapon_magnum":       dict(mode="team", per_team=1, weapons=[45],  chips=[5]),

    # -- damage chip coverage --------------------------------------------
    "chip_shock":      dict(mode="team", per_team=1, weapons=[37], chips=[1]),
    "chip_pebble":     dict(mode="team", per_team=1, weapons=[37], chips=[19]),
    "chip_ice":        dict(mode="team", per_team=1, weapons=[37], chips=[2]),
    "chip_rock":       dict(mode="team", per_team=1, weapons=[37], chips=[7]),
    "chip_spark":      dict(mode="team", per_team=1, weapons=[37], chips=[18]),
    "chip_flash":      dict(mode="team", per_team=1, weapons=[37], chips=[6]),
    "chip_stalactite": dict(mode="team", per_team=1, weapons=[37], chips=[30]),
    "chip_rockfall":   dict(mode="team", per_team=1, weapons=[37], chips=[32]),
    "chip_iceberg":    dict(mode="team", per_team=1, weapons=[37], chips=[31]),
    "chip_meteorite":  dict(mode="team", per_team=1, weapons=[37], chips=[36]),
    "chip_lightning":  dict(mode="team", per_team=1, weapons=[37], chips=[33]),
    "chip_burning":    dict(mode="team", per_team=1, weapons=[37], chips=[105]),
    "chip_plasma":     dict(mode="team", per_team=1, weapons=[37], chips=[143]),

    # -- defensive chip coverage (the AI will heal/shield itself
    #    before firing) -------------------------------------------
    "chip_heal_shield":   dict(mode="team", per_team=1, weapons=[37], chips=[20, 5]),  # shield + flame
    "chip_heal_helmet":   dict(mode="team", per_team=1, weapons=[37], chips=[21, 5]),
    "chip_heal_armor":    dict(mode="team", per_team=1, weapons=[37], chips=[22, 5]),
    "chip_heal_carapace": dict(mode="team", per_team=1, weapons=[37], chips=[81, 5]),

    # -- buff chip coverage ----------------------------------------------
    "chip_buff_rage":     dict(mode="team", per_team=1, weapons=[37], chips=[17, 5]),
    "chip_buff_reflexes": dict(mode="team", per_team=1, weapons=[37], chips=[27, 5]),
    "chip_buff_fortress": dict(mode="team", per_team=1, weapons=[37], chips=[29, 5]),
    "chip_buff_wall":     dict(mode="team", per_team=1, weapons=[37], chips=[23, 5]),
    "chip_buff_rampart":  dict(mode="team", per_team=1, weapons=[37], chips=[24, 5]),

    # -- mixed loadouts (the cases real bots hit) -------------------------
    "mixed_4v4_diverse":   dict(mode="team", per_team=4, weapons=[37, 41, 42, 277], chips=[5, 33, 7]),
    "mixed_6v6_aoe":       dict(mode="team", per_team=6, weapons=[180, 184, 43],   chips=[36, 32, 33]),
    "mixed_br10_diverse":  dict(mode="br",   n_players=10, weapons=[37, 41, 42, 38], chips=[5, 33, 6]),
}


# ---- Cell layouts -------------------------------------------------------
#
# The leek-wars-client renders cell N at pixel (px, py) computed by:
#     mod = tilesX * 2 - 1     # = 35 for an 18x18 map
#     px  = (N % mod) * 2
#     py  = (N // mod) * 2
#     if px > mod: px = px % mod; py += 1
#
# This means cell IDs *do* map directly to visible pixel coordinates
# in the diamond. Earlier layouts (using `x*18 + 17*y` with engine x in
# [3..11]) ended up packed into the upper-left quadrant -- visually
# weird, leeks all clustered, and bullets crossing dead space because
# the diamond's center is at pixel (~17, ~17), not at engine x=6.
#
# The helpers below pick cells by their *target pixel position* and
# then invert the cellToXY math to get the cell ID. This guarantees
# leeks are placed where they look like they're placed.

_MAP_W = 18
_MAP_MOD = _MAP_W * 2 - 1   # = 35
_GRID_MAX = (_MAP_W - 1) * 2  # = 34, the bounding diamond extent in pixels


def _cell_at_pixel(px: int, py: int) -> int:
    """Inverse of the client's cellToXY for an 18x18 map.

    Even rows have even-only px; odd rows have odd-only px. Caller is
    responsible for parity -- this helper picks the canonical cell at
    those pixel coords.
    """
    if py % 2 == 0:
        if px % 2 != 0:
            raise ValueError(f"even row {py} requires even px, got {px}")
        return (py // 2) * _MAP_MOD + (px // 2)
    else:
        if px % 2 == 0:
            raise ValueError(f"odd row {py} requires odd px, got {px}")
        return ((py - 1) // 2) * _MAP_MOD + _MAP_W + (px - 1) // 2


def _cells_team(team_idx: int, n: int) -> list[int]:
    """Place ``n`` leeks down a vertical column on the left/right edge.

    Team A: pixel column at x=4 (with x=6 fallbacks if more than 4
    leeks). Team B: mirrored at x=30 / x=28. Y values fan out around
    the diamond center (py ~ 17).

    Hand-picked to fit up to 8 leeks per side without colliding.
    """
    # (px, py) slots, ordered by visual priority (front leek first).
    # Constraint: even rows take even px, odd rows take odd px. We use
    # only even rows here so every column shares parity (cleaner).
    a_slots = [(4, 16), (4, 14), (4, 18), (4, 12), (4, 20),
                (6, 14), (6, 18), (6, 16)]
    b_slots = [(_GRID_MAX - sx, sy) for sx, sy in a_slots]
    slots = a_slots if team_idx == 0 else b_slots
    return [_cell_at_pixel(px, py) for (px, py) in slots[:n]]


def _cells_br(n: int) -> list[int]:
    """Distribute ``n`` leeks evenly around the diamond's center.

    Picks cells closest to the points on a circle of radius ~12 around
    pixel (17, 17). The result is a balanced spread -- no clustering,
    visible distance between every pair of leeks.
    """
    import math
    cx, cy, R = _MAP_MOD // 2, _MAP_MOD // 2, 12

    # Build pixel position for every cell once.
    nb_cells = _MAP_MOD * _MAP_W - (_MAP_W - 1)   # = 613 for 18x18
    pixel_for = []
    for cell_id in range(nb_cells):
        px = (cell_id % _MAP_MOD) * 2
        py = (cell_id // _MAP_MOD) * 2
        if px > _MAP_MOD:
            px = px % _MAP_MOD
            py += 1
        pixel_for.append((px, py))

    picks = []
    for k in range(n):
        angle = 2 * math.pi * k / n
        tx = cx + R * math.cos(angle)
        ty = cy + R * math.sin(angle)
        # Find closest valid cell that isn't already picked.
        best = None; best_d = 1e18
        for cell_id, (px, py) in enumerate(pixel_for):
            if cell_id in picks: continue
            d = (px - tx) ** 2 + (py - ty) ** 2
            if d < best_d:
                best_d = d; best = cell_id
        picks.append(best)
    return picks


# ---- Engine setup helpers ----------------------------------------------

def _add_weapon(eng, weapon_id: int) -> None:
    raw = base.WEAPONS[weapon_id]
    eng.add_weapon(
        item_id=weapon_id, name=raw["name"], cost=int(raw["cost"]),
        min_range=int(raw["min_range"]), max_range=int(raw["max_range"]),
        launch_type=int(raw["launch_type"]), area_id=int(raw["area"]),
        los=bool(raw.get("los", True)), max_uses=int(raw.get("max_uses", -1)),
        effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                  int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                 for e in raw["effects"]],
        passive_effects=[(int(e["id"]), float(e["value1"]), float(e["value2"]),
                          int(e["turns"]), int(e["targets"]), int(e["modifiers"]))
                         for e in raw.get("passive_effects", [])],
    )


def _setup_engine(weapons: list[int]) -> _v2.Engine:
    eng = _v2.Engine()
    for wid in weapons:
        _add_weapon(eng, wid)
    base.register_all_chips_v2(eng)
    return eng


# ---- AI ----------------------------------------------------------------
# Generic AI: pick nearest enemy, move toward it, cast first chip if it
# hits, then fire weapon. Same structure as test_v2_team_real but with
# multi-chip fallback (some chips need LOS, some don't, etc.).

def _make_ai(eng, weapon_id: int, chip_ids: list[int]):
    """Generic AI that respects the weapon's min_range.

    Most weapons we test (laser, lightninger, bazooka...) have a
    non-zero ``min_range``. ``move_toward`` walks all the way to an
    adjacent cell, which puts the leek *inside* min_range and the
    weapon refuses to fire. We compute ``stop_distance = min_range``
    and pass it as a budget cap to ``move_toward``: stop closing when
    we're already inside [min_range, max_range].
    """
    weapon = base.WEAPONS[weapon_id]
    min_r = int(weapon["min_range"])
    max_r = int(weapon["max_range"])

    def ai(idx, turn):
        n = eng.n_entities()
        my_team = eng.entity_team(idx)
        my_cell = eng.entity_cell(idx)
        target = None; bd = 10**9
        for i in range(n):
            if i == idx or not eng.entity_alive(i): continue
            if eng.entity_team(i) == my_team: continue
            d = eng.cell_distance2(my_cell, eng.entity_cell(i))
            if d < bd: bd = d; target = i
        if target is None: return 0
        target_cell = eng.entity_cell(target)

        # Move toward enemy *but not past max_range* -- the engine's
        # move_toward defaults to "go adjacent", which puts melee
        # weapons in range but long-range weapons (range 7-9, 8-12)
        # well inside their min_range, where rc=-4. Cap the MP budget
        # at the distance over max_range so we stop in the firing band.
        cur_d = eng.cell_distance(my_cell, target_cell)
        if cur_d > max_r:
            budget = min(6, cur_d - max_r)
            eng.move_toward(idx, target_cell, max_mp=budget)
        elif cur_d < min_r:
            # We're already too close. Engine doesn't expose a
            # "back away" primitive, so skip the move and hope the
            # enemy moves -- or we cast chips this turn.
            pass
        # else: already in [min_range, max_range], don't move.

        # Try every chip in turn (heal+damage stacks: heal hits self,
        # damage hits enemy) -- ignore failures, the engine returns 0 on
        # range/los/cooldown rejection.
        for cid in chip_ids:
            if not eng.entity_alive(target): break
            target_cell = eng.entity_cell(target)
            chip = base.CHIPS.get(cid, {})
            eff_ids = {int(e["id"]) for e in chip.get("effects", [])}
            # Self-cast for healing/buff chips (effects 6 = HEAL, 5 =
            # SHIELD, 4 = BOOST_AGI, 8 = BOOST_STR, 11 = BOOST_FREQ).
            self_cast = bool(eff_ids & {4, 5, 6, 8, 11, 26})
            cast_cell = my_cell if self_cast else target_cell
            eng.use_chip(idx, cid, cast_cell)
        # Fire the (first) weapon up to 8 times.
        for _ in range(8):
            if not eng.entity_alive(target): break
            target_cell = eng.entity_cell(target)
            rc = eng.fire_weapon(idx, weapon_id, target_cell)
            if rc <= 0: break
        return 0
    return ai


# ---- Scenario runners --------------------------------------------------

def run_team(per_team: int, weapons: list[int], chips: list[int],
              seed: int = 1, max_turns: int = 25) -> tuple[_v2.Engine, list[dict], list[int], list[int]]:
    """Run a per_team-vs-per_team team fight."""
    eng = _setup_engine(weapons)
    cells_a = _cells_team(0, per_team)
    cells_b = _cells_team(1, per_team)

    eng.add_team(1, "TA"); eng.add_team(2, "TB")
    fid = 1
    leeks = []
    for i in range(per_team):
        eng.add_farmer(fid, f"A{i+1}", "fr")
        eng.add_entity(team=0, fid=fid, name=f"A{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=0,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=fid, team_id=1, weapons=weapons,
                       chips=chips, cell=cells_a[i])
        leeks.append(build_leek(id=fid - 1, team=1, name=f"A{i+1}", cell=cells_a[i],
                                weapons=weapons, chips=chips, farmer=fid))
        fid += 1
    for i in range(per_team):
        eng.add_farmer(fid, f"B{i+1}", "fr")
        eng.add_entity(team=1, fid=fid, name=f"B{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=0,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=fid, team_id=2, weapons=weapons,
                       chips=chips, cell=cells_b[i])
        leeks.append(build_leek(id=fid - 1, team=2, name=f"B{i+1}", cell=cells_b[i],
                                weapons=weapons, chips=chips, farmer=fid))
        fid += 1

    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_type(TYPE_TEAM if per_team > 1 else TYPE_SOLO)
    eng.set_custom_map(obstacles={}, team1=cells_a, team2=cells_b)
    eng.set_ai_callback(_make_ai(eng, weapons[0], chips))
    eng.run()

    team1_ids = [leek["id"] for leek in leeks if leek["team"] == 1]
    team2_ids = [leek["id"] for leek in leeks if leek["team"] == 2]
    return eng, leeks, team1_ids, team2_ids


def run_br(n_players: int, weapons: list[int], chips: list[int],
            seed: int = 1, max_turns: int = 25) -> tuple[_v2.Engine, list[dict], list[int], list[int]]:
    """Run an n_players-way battle royale."""
    eng = _setup_engine(weapons)
    cells = _cells_br(n_players)

    leeks = []
    for i in range(n_players):
        eng.add_team(i + 1, f"T{i+1}")
        eng.add_farmer(i + 1, f"P{i+1}", "fr")
    for i in range(n_players):
        eng.add_entity(team=i, fid=i + 1, name=f"P{i+1}", level=100,
                       life=2500, tp=14, mp=6, strength=200, agility=0,
                       frequency=100, wisdom=0, resistance=0, science=0,
                       magic=0, cores=10, ram=10,
                       farmer=i + 1, team_id=i + 1, weapons=weapons,
                       chips=chips, cell=cells[i])
        # In BR, every entity is its own team. The leek-wars-client
        # handles BR by checking fight.type === BATTLE_ROYALE; we still
        # set leek.team = i + 1 so each gets a distinct color.
        leeks.append(build_leek(id=i, team=i + 1, name=f"P{i+1}", cell=cells[i],
                                weapons=weapons, chips=chips, farmer=i + 1))

    eng.set_seed(seed); eng.set_max_turns(max_turns)
    eng.set_type(TYPE_BATTLE_ROYALE)
    # BR custom map: pass all cells as team1, team2 empty.
    eng.set_custom_map(obstacles={}, team1=cells, team2=[])
    eng.set_ai_callback(_make_ai(eng, weapons[0], chips))
    eng.run()

    # In BR mode the report.fight.team1/team2 split is mostly cosmetic --
    # the client's fight-summary screen uses it to bucket leeks. For
    # rendering purposes we just need every leek to be in EITHER list.
    team1_ids = [leek["id"] for leek in leeks]
    team2_ids: list[int] = []
    return eng, leeks, team1_ids, team2_ids


def run_scenario(name: str, seed: int = 1, max_turns: int = 25):
    cfg = SCENARIOS[name]
    if cfg["mode"] == "team":
        # The new pixel-based team layout puts teams at x=4 vs x=30 --
        # ~13 cells apart, which fits every weapon's max_range (the
        # longest is bazooka 8-12, comfortable). No need for a "wide"
        # variant anymore.
        return run_team(cfg["per_team"], cfg["weapons"], cfg["chips"],
                         seed=seed, max_turns=cfg.get("max_turns", max_turns))
    elif cfg["mode"] == "br":
        return run_br(cfg["n_players"], cfg["weapons"], cfg["chips"],
                       seed=seed, max_turns=cfg.get("max_turns", max_turns))
    raise ValueError(f"unknown mode {cfg['mode']!r}")


# ---- CLI ---------------------------------------------------------------

def _print_list() -> None:
    print("Available scenarios:")
    longest = max(len(n) for n in SCENARIOS) + 2
    for name, cfg in SCENARIOS.items():
        if cfg["mode"] == "team":
            kind = f"{cfg['per_team']}v{cfg['per_team']} team"
        else:
            kind = f"BR {cfg['n_players']}"
        ws = ",".join(str(w) for w in cfg["weapons"])
        cs = ",".join(str(c) for c in cfg["chips"])
        print(f"  {name:<{longest}}  [{kind:<10}]  weapons={ws}  chips={cs}")
    print(f"\nTotal: {len(SCENARIOS)} scenarios.")


def _run_one(name: str, *, out: str, seed: int, turns: int) -> tuple[int, int]:
    eng, leeks, t1, t2 = run_scenario(name, seed=seed, max_turns=turns)
    report = build_report(eng, leeks, team1_ids=t1, team2_ids=t2)
    write_report(out, report)
    return len(report["fight"]["actions"]), int(report.get("winner", -1))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--list", action="store_true", help="list scenarios and exit")
    p.add_argument("--scenario", default=None,
                    help="name of scenario to run (see --list); 'all' "
                         "runs every scenario in turn (no viewer, just "
                         "validates the engine doesn't crash)")
    p.add_argument("--out", default=DEFAULT_REPORT,
                    help="path to write report.json")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--turns", type=int, default=25)
    args = p.parse_args()

    if args.list:
        _print_list()
        return

    if args.scenario is None:
        p.error("--scenario is required (or use --list)")

    if args.scenario == "all":
        # Run every scenario, dumping per-scenario reports under /tmp.
        # This is the engine-only smoke test (the viewer can only show
        # one report at a time).
        out_dir = os.path.join(os.path.dirname(args.out), "stress")
        os.makedirs(out_dir, exist_ok=True)
        print(f"Running ALL {len(SCENARIOS)} scenarios -> {out_dir}/")
        ok = 0; failed = []
        for name in SCENARIOS:
            out = os.path.join(out_dir, f"{name}.json")
            try:
                n, w = _run_one(name, out=out, seed=args.seed, turns=args.turns)
                print(f"  {name:<32}  actions={n:>5}  winner={w}")
                ok += 1
            except Exception as ex:
                print(f"  {name:<32}  FAILED ({type(ex).__name__}: {ex})")
                failed.append((name, ex))
        print(f"\n{ok}/{len(SCENARIOS)} scenarios completed.")
        if failed:
            print("Failed:")
            for name, ex in failed:
                print(f"  {name}: {ex}")
        return

    if args.scenario not in SCENARIOS:
        p.error(f"unknown scenario {args.scenario!r}; --list to see all")

    n, w = _run_one(args.scenario, out=args.out, seed=args.seed, turns=args.turns)
    print(f"wrote {args.out}")
    print(f"  scenario: {args.scenario}")
    print(f"  actions:  {n}")
    print(f"  winner:   {w}")


if __name__ == "__main__":
    main()
