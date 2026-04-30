"""leekwars_c -- Python bindings for the C engine.

The compiled module ``_engine`` ships the State, Action, Map, and
Entity wrappers, plus standalone helpers (clone, legal_actions, ...)
that mirror the Python reference engine's public surface.

Build the extension with::

    python bindings/python/setup.py build_ext --inplace

(see ``bindings/python/setup.py`` at the repo root).
"""
from ._engine import (
    State,
    Action,
    InventoryProfile,
    ActionType,
    LW_MAX_INVENTORY,
    LW_MAX_PATH_LEN,
    LW_MAX_CELLS,
    LW_MAX_ENTITIES,
)

__all__ = [
    "State",
    "Action",
    "InventoryProfile",
    "ActionType",
    "LW_MAX_INVENTORY",
    "LW_MAX_PATH_LEN",
    "LW_MAX_CELLS",
    "LW_MAX_ENTITIES",
]
