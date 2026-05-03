"""V2 mass chip sweep across all chips with simple AI."""
from __future__ import annotations
import argparse, json, os, sys
import leekwars_c_v2._engine as _v2

PY_DIR = "C:/Users/aurel/Desktop/leekwars_generator_python"
if PY_DIR not in sys.path: sys.path.insert(0, PY_DIR)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_v2_chip as base


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seeds", type=int, default=10)
    args = p.parse_args()

    chips_by_id = {int(s["id"]): s for s in
                    json.load(open(os.path.join(PY_DIR, "data", "chips.json"))).values()}
    # All chips with min_range == 0 (self-castable) so the test AI's
    # cast-on-self pattern works.
    candidates = sorted([cid for cid, c in chips_by_id.items()
                          if c.get("min_range") == 0])

    print(f"Mass chip sweep: {len(candidates)} chips x {args.seeds} seeds\n")
    n_pass = 0; n_total = 0; failures = []
    for cid in candidates:
        chip = chips_by_id[cid]
        n_ok = 0
        for s in range(1, args.seeds + 1):
            try:
                v_stream, vw = base.run_v2(s, 37, cid, 72, 144)
                p_stream, pw = base.run_py(s, 37, cid, 72, 144)
            except Exception:
                continue
            vn = base.normalize(v_stream, "v2"); pn = base.normalize(p_stream, "py")
            f, an, bn, av, pv = base.diff(vn, pn)
            if f == -1:
                n_ok += 1
            else:
                if len(failures) < 3:
                    failures.append((cid, chip["name"], s, f, av, pv))
        n_total += args.seeds
        n_pass += n_ok
        status = "OK" if n_ok == args.seeds else f"{n_ok}/{args.seeds}"
        print(f"  chip {cid:>4} ({chip['name']:<28}): {status}")

    print(f"\nTOTAL: {n_pass}/{n_total} byte-identical ({100*n_pass//max(1,n_total)}%)")
    for cid, name, seed, idx, av, pv in failures:
        print(f"  chip {cid} ({name}) seed {seed} @ idx {idx}")
        print(f"    v2: {av}")
        print(f"    py: {pv}")


if __name__ == "__main__":
    main()
