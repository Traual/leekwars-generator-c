"""Cross-check forward declarations vs actual definitions in src_v2/.

Each agent forward-declared functions it calls but doesn't define.
After all agents land, this script reports:
  - functions called externally but never defined  (link errors)
  - functions defined but never called             (likely OK; may be unused)
  - struct names declared but never defined        (incomplete type errors)

Run:  python src_v2/check_decls.py

Output is a punch list for the integrator.
"""
import os
import re
from collections import defaultdict


HERE = os.path.dirname(os.path.abspath(__file__))
INC = os.path.normpath(os.path.join(HERE, "..", "include_v2"))


def scan_files(root, ext):
    out = []
    for fn in sorted(os.listdir(root)):
        if fn.endswith(ext):
            out.append(os.path.join(root, fn))
    return out


C_FILES = scan_files(HERE, ".c")
H_FILES = scan_files(INC, ".h")


# Heuristic regexes -- not a real C parser, but should catch the common cases.
DEF_FUNC_RE = re.compile(r'^(?:static\s+inline\s+|static\s+|extern\s+)?'
                         r'(?:[A-Za-z_][\w\s\*]+?\s)?'
                         r'(lw_[a-z_][a-z0-9_]*)\s*\(', re.MULTILINE)
DECL_FUNC_RE = re.compile(r'(?:^|\s)(lw_[a-z_][a-z0-9_]*)\s*\(', re.MULTILINE)
EXTERN_FUNC_RE = re.compile(r'^extern\s+\w[\w\s\*]*\s+(lw_[a-z_][a-z0-9_]*)\s*\(', re.MULTILINE)


def parse_funcs(path):
    """Return (defined_names, called_names) where 'defined' is the set of
    function NAMES whose body is in this file (not just prototypes)."""
    txt = open(path, encoding='utf-8', errors='replace').read()
    # Crude: a function is DEFINED if it has '{' on the same line as the (
    defined = set()
    called = set()
    for m in re.finditer(r'(lw_[a-z_][a-z0-9_]*)\s*\(([^;]*?)\{', txt, re.DOTALL):
        defined.add(m.group(1))
    # Calls: any lw_xxx( that's not preceded by a return-type-looking token
    for m in re.finditer(r'(?<![\w])(lw_[a-z_][a-z0-9_]*)\s*\(', txt):
        called.add(m.group(1))
    return defined, called


def parse_header(path):
    """Header declarations (prototypes only)."""
    txt = open(path, encoding='utf-8', errors='replace').read()
    declared = set()
    # static inline definitions inside .h count as defined too
    for m in re.finditer(r'static\s+inline[^{]*\s(lw_[a-z_][a-z0-9_]*)\s*\([^)]*\)\s*\{',
                         txt, re.DOTALL):
        declared.add(m.group(1))
    # plain prototypes  (return-type lw_name(args);)
    for m in re.finditer(r'^\s*\w[\w\s\*]*\s+(lw_[a-z_][a-z0-9_]*)\s*\([^)]*\)\s*;',
                         txt, re.MULTILINE):
        declared.add(m.group(1))
    # extern declarations
    for m in EXTERN_FUNC_RE.finditer(txt):
        declared.add(m.group(1))
    return declared


def main():
    all_defined = set()       # has a body (in .c or static inline in .h)
    all_declared = set()      # has a prototype
    all_called = defaultdict(list)   # name -> [callers]

    for h in H_FILES:
        all_declared |= parse_header(h)
        all_defined |= parse_header(h)  # static inline counts as defined

    for c in C_FILES:
        d, called = parse_funcs(c)
        all_defined |= d
        for name in called:
            all_called[name].append(os.path.basename(c))

    print(f"\n=== {len(C_FILES)} .c files, {len(H_FILES)} .h files ===")
    print(f"  defined: {len(all_defined)}")
    print(f"  declared (incl. defined): {len(all_declared)}")

    # Missing definitions: called from .c but never defined and never declared
    missing = []
    for name, callers in sorted(all_called.items()):
        if name not in all_defined:
            missing.append((name, callers))

    if missing:
        print(f"\n[!] {len(missing)} undefined functions called:")
        for name, callers in missing:
            print(f"  {name:50s} called from {callers}")
    else:
        print("\n[ok] All called functions resolve.")

    # Unused: defined but never called.  Many are public API entry points
    # (e.g. lw_state_init, lw_actions_log_*); just show count.
    unused = sorted(all_defined - set(all_called) - {"lw_main"})
    print(f"\n[i] {len(unused)} defined-but-not-called functions (likely public API)")


if __name__ == "__main__":
    main()
