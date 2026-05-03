import sys
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")
sys.path.insert(0, ".")
import test_v2_mass_chips_all as t
import test_v2_chip as base

# chip 5 flame, seed 1
v_stream, vw = t.run_v2_smart(1, 37, 5, 72, 144)
p_stream, pw = t.run_py_smart(1, 37, 5, 72, 144)
vn = base.normalize(v_stream, "v2")
pn = base.normalize(p_stream, "py")
print(f"v2 n={len(vn)} winner={vw}")
print(f"py n={len(pn)} winner={pw}")
# Find first divergence
n = min(len(vn), len(pn))
for i in range(n):
    if vn[i] != pn[i]:
        print(f"DIVERGE @ {i}")
        for j in range(max(0, i-5), min(n, i+5)):
            mark = ">>" if j == i else "  "
            print(f"  {mark} {j:>3}  v2={vn[j]}  py={pn[j]}")
        break
else:
    if len(vn) != len(pn):
        print(f"len diff: v2={len(vn)} py={len(pn)}")
        # Print last few
        for j in range(n-3, n+5):
            v = vn[j] if j < len(vn) else "EOF"
            p = pn[j] if j < len(pn) else "EOF"
            print(f"  {j:>3}  v2={v}  py={p}")
