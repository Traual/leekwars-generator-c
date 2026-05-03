"""Convert a v2 engine action stream into the JSON envelope that the
official leek-wars-client expects, so we can watch our fights locally.

The leek-wars-client (Vue.js) has a built-in "local" mode: navigating to
``/fight/local`` makes it ``fetch('/static/report.json')`` and play that
fight with the full sprite + animation rig. Since our C engine emits
the exact same action-stream format as the upstream Java engine, we
just need to wrap it in the right envelope.

Usage:
    from leekwars_c.replay import build_report, write_report, build_leek

    leeks = [
        build_leek(id=0, team=1, name="A", cell=72, weapons=[37], chips=[]),
        build_leek(id=1, team=2, name="B", cell=144, weapons=[37], chips=[]),
    ]
    report = build_report(eng, leeks, team1_ids=[0], team2_ids=[1])
    write_report("path/to/leek-wars/public/static/report.json", report)
"""
from __future__ import annotations

import json
import os
from typing import Any


# Action codes that pack a path / list as a *nested array* in the player JSON.
# Java's getJSON for these emits the trailing arg as a sub-array, not flat.
_NESTED_EXTRA_ACTIONS = {
    10,   # MOVE_TO -> [10, leek, end, [path...]]
}


def _to_player_action(event: dict) -> list:
    """v2 ``{type, args, extra}`` -> player ``[type, *args]`` (or with nested
    extra for actions that need it)."""
    t = event["type"]
    args = list(event.get("args", []))
    extra = list(event.get("extra", []))
    if extra and t in _NESTED_EXTRA_ACTIONS:
        return [t, *args, extra]
    if extra:
        return [t, *args, *extra]
    return [t, *args]


def build_leek(
    *,
    id: int,
    team: int,
    name: str,
    cell: int,
    level: int = 100,
    life: int = 2500,
    strength: int = 200,
    agility: int = 0,
    wisdom: int = 0,
    resistance: int = 0,
    science: int = 0,
    magic: int = 0,
    frequency: int = 100,
    tp: int = 14,
    mp: int = 6,
    weapons: list[int] | None = None,
    chips: list[int] | None = None,
    skin: int = 1,
    hat: int | None = None,
    farmer: int | None = None,
    summon: bool = False,
    orientation: int = 0,
) -> dict:
    """Build one entry of ``fight.leeks`` matching what the Java engine emits.

    Mirrors EntityInfo.toJson() in the Java reference: include every field
    the client looks at (orientation matters!), and use sensible defaults
    matching Java's null-vs-zero handling (hat=null when no hat).
    """
    leek = {
        "id": id,
        "team": team,
        "name": name,
        "level": level,
        "life": life,
        "strength": strength,
        "agility": agility,
        "wisdom": wisdom,
        "resistance": resistance,
        "science": science,
        "magic": magic,
        "frequency": frequency,
        "tp": tp,
        "mp": mp,
        "cellPos": cell,
        "weapons": weapons or [],
        "chips": chips or [],
        "skin": skin,
        "hat": hat,
        "metal": level >= 80,
        "face": 2 if level >= 20 else 0,
        "summon": summon,
        "type": 0,
        "orientation": orientation,
    }
    if farmer is not None:
        leek["farmer"] = farmer
    return leek


def build_report(
    engine,
    leeks: list[dict],
    team1_ids: list[int],
    team2_ids: list[int],
    *,
    obstacles: dict[int, int] | None = None,
    map_id: int = 0,
    map_type: int = -1,           # Nexus arena (CONTEXT_TEST)
    map_w: int = 18,
    map_h: int = 18,
    pattern: list[int] | None = None,
    farmers1: dict[int, dict] | None = None,
    farmers2: dict[int, dict] | None = None,
    winner: int | None = None,
    ai_logs: dict | None = None,
) -> dict:
    """Wrap a v2 engine into the report.json envelope expected by leek-wars-client.

    Pass the engine *after* it has been ``run()``. Optionally pass
    farmers1 / farmers2 dicts (else a single placeholder farmer per team
    is generated from the leek list).
    """
    actions = [_to_player_action(e) for e in engine.stream_dump()]

    if farmers1 is None or farmers2 is None:
        # Build placeholders from the leek list.
        f1 = {}
        f2 = {}
        for leek in leeks:
            farmer_id = leek.get("farmer", leek["id"])
            entry = {"id": farmer_id, "name": leek["name"]}
            (f1 if leek["team"] == 1 else f2)[farmer_id] = entry
        farmers1 = farmers1 or f1
        farmers2 = farmers2 or f2

    if winner is None:
        try:
            winner = int(getattr(engine, "winner", -1))
        except Exception:
            winner = -1

    # Java's getJSON puts leeks/map/actions/team1/team2/ops/dead directly
    # under "fight" (no nested "data"). The leek-wars-client copies
    # `report.fight` straight into `Fight.data` (which has shape FightData).
    return {
        "fight": {
            "leeks": leeks,
            "map": {
                "id": map_id,
                "width": map_w,
                "height": map_h,
                "obstacles": obstacles or {},
                "type": map_type,
                "pattern": pattern or [],
                "players": {},
            },
            "actions": actions,
            "team1": team1_ids,
            "team2": team2_ids,
            "ops": {leek["id"]: 0 for leek in leeks},
            "dead": {leek["id"]: False for leek in leeks},
        },
        "logs": ai_logs or {},
        # Optional metadata used by the wrapper Fight object (player.vue
        # builds its local_fight envelope around report.fight).
        "winner": winner,
        "farmers1": farmers1,
        "farmers2": farmers2,
        "leeks1": [leek for leek in leeks if leek["team"] == 1],
        "leeks2": [leek for leek in leeks if leek["team"] == 2],
    }


def write_report(path: str, report: dict) -> None:
    """Write report.json to *path*. Creates parent dirs as needed."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f)
