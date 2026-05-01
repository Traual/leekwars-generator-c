"""Movement-effect parity test: Push / Attract destinations.

We don't instantiate the full upstream Python ``Map``. Instead, we
build a small grid in pure Python whose semantics mirror what
``Map.getPushLastAvailableCell`` / ``getAttractLastAvailableCell``
read (cell.x, cell.y, cell.walkable, map.getEntity, cell.next). The
Python algorithm is short enough that we transcribe it verbatim
from leekwars/maps/map.py — the Java engine has the same code, so a
match here is parity against both references.

Then we exercise the same setup against my C
``lw_compute_push_dest`` / ``lw_compute_attract_dest``.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import leekwars_c as lwc
from leekwars_c._engine import State, Topology


def py_push_dest(ex, ey, tx, ty, cx, cy, walkable_xy, occupied_xy):
    """Mirrors Map.getPushLastAvailableCell line-for-line."""
    sgn = lambda v: 1 if v > 0 else (-1 if v < 0 else 0)
    cdx = sgn(ex - cx); cdy = sgn(ey - cy)
    dx = sgn(tx - ex); dy = sgn(ty - ey)
    if cdx != dx or cdy != dy:
        return ex, ey
    cur_x, cur_y = ex, ey
    while (cur_x, cur_y) != (tx, ty):
        nx, ny = cur_x + dx, cur_y + dy
        if not walkable_xy(nx, ny):
            return cur_x, cur_y
        if occupied_xy(nx, ny):
            return cur_x, cur_y
        cur_x, cur_y = nx, ny
    return cur_x, cur_y


def py_attract_dest(ex, ey, tx, ty, cx, cy, walkable_xy, occupied_xy):
    """Mirrors Map.getAttractLastAvailableCell line-for-line.
    Direction is reversed: entity moves TOWARD caster."""
    sgn = lambda v: 1 if v > 0 else (-1 if v < 0 else 0)
    cdx = sgn(ex - cx); cdy = sgn(ey - cy)
    dx = sgn(tx - ex); dy = sgn(ty - ey)
    # For attract, caster->entity must be opposite of entity->target
    if cdx != -dx or cdy != -dy:
        return ex, ey
    cur_x, cur_y = ex, ey
    while (cur_x, cur_y) != (tx, ty):
        nx, ny = cur_x + dx, cur_y + dy
        if not walkable_xy(nx, ny):
            return cur_x, cur_y
        if occupied_xy(nx, ny):
            return cur_x, cur_y
        cur_x, cur_y = nx, ny
    return cur_x, cur_y


# ---- Build a 15x15 grid topology in C -----------------------------

def build_15x15_topology():
    cells = []
    neighbors = []
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


def fuzz_push_attract(n_cases: int) -> tuple[int, int]:
    rng = random.Random(0)
    topo, W, H = build_15x15_topology()
    push_fails = attract_fails = 0

    for trial in range(n_cases):
        # Random caster, entity (the one being moved), target (epicenter
        # the push aims toward). To exercise the geometry properly we
        # bias toward configurations where caster, entity, target are
        # collinear (50%); otherwise random.
        coll = rng.random() < 0.5
        if coll:
            # Pick axis + caster pos. entity in front of caster, target further.
            axis = rng.choice(["x", "y"])
            cx = rng.randint(0, W - 1); cy = rng.randint(0, H - 1)
            d = rng.choice([-1, 1])
            offsets = sorted([rng.randint(1, 5), rng.randint(1, 7)])
            if axis == "x":
                ex = cx + d * offsets[0]; ey = cy
                tx = cx + d * offsets[1]; ty = cy
            else:
                ex = cx; ey = cy + d * offsets[0]
                tx = cx; ty = cy + d * offsets[1]
        else:
            cx = rng.randint(0, W - 1); cy = rng.randint(0, H - 1)
            ex = rng.randint(0, W - 1); ey = rng.randint(0, H - 1)
            tx = rng.randint(0, W - 1); ty = rng.randint(0, H - 1)

        # Random obstacles on the grid (5% obstruction rate).
        obstacles = set()
        for _ in range(rng.randint(0, 8)):
            ox = rng.randint(0, W - 1); oy = rng.randint(0, H - 1)
            if (ox, oy) not in {(cx, cy), (ex, ey), (tx, ty)}:
                obstacles.add((ox, oy))

        # Skip if any of the three positions is invalid (off-grid).
        if not (0 <= cx < W and 0 <= cy < H and
                0 <= ex < W and 0 <= ey < H and
                0 <= tx < W and 0 <= ty < H):
            continue

        # Set up C state with the topology + entity occupancy.
        s = State()
        s.set_topology(topo)
        # Mark obstacles as non-walkable. Topology returned from
        # from_grid has all walkable=True; we need to mutate. The
        # binding doesn't expose per-cell walkable mutation, so we use
        # entity occupancy as a proxy: place a dummy "occupier" entity
        # on each obstacle cell.
        # Caster + entity-being-moved are real entities.
        c_idx = ec_idx = -1
        # entity 0 = caster
        s.add_entity(fid=1, team_id=0, cell_id=cy * W + cx,
                     total_tp=10, total_mp=4, hp=1000, total_hp=1000,
                     weapons=[], chips=[])
        c_idx = 0
        # entity 1 = the one being pushed (only if different cell)
        if (ex, ey) != (cx, cy):
            s.add_entity(fid=2, team_id=1, cell_id=ey * W + ex,
                         total_tp=10, total_mp=4, hp=1000, total_hp=1000,
                         weapons=[], chips=[])
            ec_idx = 1
        # obstacles as occupier entities (unaligned team)
        next_fid = 3
        for ox, oy in obstacles:
            s.add_entity(fid=next_fid, team_id=2, cell_id=oy * W + ox,
                         total_tp=10, total_mp=4, hp=1000, total_hp=1000,
                         weapons=[], chips=[])
            next_fid += 1

        if ec_idx < 0:
            continue  # caster == entity_to_move — push is undefined

        # Python helpers.
        def walkable_xy(x, y):
            return 0 <= x < W and 0 <= y < H
        def occupied_xy(x, y):
            return (x, y) in obstacles or (x, y) == (cx, cy)

        # ---- Push ----
        py_dx, py_dy = py_push_dest(ex, ey, tx, ty, cx, cy,
                                      walkable_xy, occupied_xy)
        py_dest_cell = py_dy * W + py_dx
        c_dest_cell = s._compute_push_dest(ey * W + ex,
                                             ty * W + tx,
                                             cy * W + cx)
        if c_dest_cell != py_dest_cell:
            push_fails += 1
            if push_fails <= 3:
                print(f"  push trial {trial}: c={(cx,cy)} e={(ex,ey)} "
                      f"t={(tx,ty)} obs={obstacles}")
                print(f"    C={c_dest_cell} ({c_dest_cell % W},{c_dest_cell // W}) "
                      f"PY={py_dest_cell} ({py_dx},{py_dy})")

        # ---- Attract ----
        py_ax, py_ay = py_attract_dest(ex, ey, tx, ty, cx, cy,
                                          walkable_xy, occupied_xy)
        py_attract_cell = py_ay * W + py_ax
        c_attract_cell = s._compute_attract_dest(ey * W + ex,
                                                   ty * W + tx,
                                                   cy * W + cx)
        if c_attract_cell != py_attract_cell:
            attract_fails += 1
            if attract_fails <= 3:
                print(f"  attract trial {trial}: c={(cx,cy)} e={(ex,ey)} "
                      f"t={(tx,ty)} obs={obstacles}")
                print(f"    C={c_attract_cell} PY={py_attract_cell}")

    return push_fails, attract_fails


def main():
    n_cases = 2000
    push_fails, attract_fails = fuzz_push_attract(n_cases)
    print(f"  push    {n_cases:>5} cases   "
          f"{'PASS' if push_fails == 0 else f'FAIL ({push_fails})'}")
    print(f"  attract {n_cases:>5} cases   "
          f"{'PASS' if attract_fails == 0 else f'FAIL ({attract_fails})'}")
    if push_fails or attract_fails:
        sys.exit(1)


if __name__ == "__main__":
    main()
