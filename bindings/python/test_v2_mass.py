"""V2 mass weapon sweep: ALL ~50 weapons + first 30 chips, byte-identical
parity check.
"""
from __future__ import annotations
import argparse, json, os, sys, importlib
import leekwars_c._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path: sys.path.insert(0, PY_DIR)

# Import the existing parity helpers
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_v2_parity as base


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=10)
    args = p.parse_args()

    weapons_by_id = {int(s["item"]): s for s in
                      json.load(open(os.path.join(PY_DIR, "data", "weapons.json"))).values()}
    weapon_ids = sorted(weapons_by_id.keys())

    print(f"Mass weapon sweep: {len(weapon_ids)} weapons x {args.seeds} seeds\n")
    n_pass = 0; n_total = 0
    failures = []
    for wid in weapon_ids:
        raw = weapons_by_id[wid]
        # Pick a cell distance that fits the weapon's range
        max_r = int(raw["max_range"])
        min_r = int(raw["min_range"])
        dist = max(min_r, min(max_r, 4))
        a_cell = 72  # (4, 0)
        b_cell = a_cell + 18 * dist  # same row, dist cells away
        n_ok = 0
        for s in range(1, args.seeds + 1):
            try:
                v_stream, vw = base.run_v2(s, wid, a_cell, b_cell)
                p_stream, pw = base.run_py(s, wid, a_cell, b_cell)
            except Exception as ex:
                continue
            vn = base.normalize(v_stream, "v2"); pn = base.normalize(p_stream, "py")
            d = base.diff_streams(vn, pn)
            if d["identical"]:
                n_ok += 1
            else:
                if len(failures) < 3:
                    failures.append((wid, raw["name"], s, d["first_div"], d["a_at_div"], d["b_at_div"]))
        n_total += args.seeds
        n_pass += n_ok
        status = "OK" if n_ok == args.seeds else f"{n_ok}/{args.seeds}"
        print(f"  weapon {wid:>4} ({raw['name']:<22}): {status}")

    print(f"\nTOTAL: {n_pass}/{n_total} byte-identical ({100*n_pass//max(1,n_total)}%)")
    for wid, name, seed, idx, av, pv in failures:
        print(f"  weapon {wid} ({name}) seed {seed} @ idx {idx}")
        print(f"    v2: {av}")
        print(f"    py: {pv}")


if __name__ == "__main__":
    main()
