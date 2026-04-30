"""Build the Python bindings for the C engine.

Run from the repo root:

    python bindings/python/setup.py build_ext --inplace

This compiles ``leekwars_c/_engine.pyx`` against the static library
in ``../../src/`` and drops the .pyd / .so next to the .pyx file.
"""
import glob
import os
import sys

from setuptools import setup, Extension
from Cython.Build import cythonize


HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


# Glob every src/lw_*.c so newly-added modules get picked up
# automatically. We deliberately don't list them by hand because the
# port is moving fast and a missed module would hide as a link error.
C_SOURCES = sorted(glob.glob(os.path.join(ROOT, "src", "lw_*.c")))

INCLUDE_DIRS = [
    os.path.join(ROOT, "include"),
]


extensions = [
    Extension(
        name="leekwars_c._engine",
        sources=[
            os.path.join(HERE, "leekwars_c", "_engine.pyx"),
            *C_SOURCES,
        ],
        include_dirs=INCLUDE_DIRS,
        extra_compile_args=["/O2"] if sys.platform == "win32" else ["-O2"],
        # libm is needed for pow() in lw_order.c on Linux/Mac.
        libraries=[] if sys.platform == "win32" else ["m"],
        language="c",
    ),
]


setup(
    name="leekwars_c",
    version="0.0.1",
    packages=["leekwars_c"],
    package_dir={"leekwars_c": os.path.join(HERE, "leekwars_c")},
    ext_modules=cythonize(
        extensions,
        compiler_directives={
            "language_level": "3",
            "boundscheck": False,
            "wraparound": False,
            "initializedcheck": False,
            "cdivision": True,
        },
    ),
)
