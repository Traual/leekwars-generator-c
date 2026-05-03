"""Build the Python bindings against the line-by-line Java port.

The engine lives in src/ + include/.  This setup compiles every
src/lw_*.c into a single C extension named ``leekwars_c._engine``.

Run from the repo root:

    python bindings/python/setup.py build_ext --inplace
"""
import glob
import os
import sys

from setuptools import setup, Extension
from Cython.Build import cythonize


HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


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
        libraries=[] if sys.platform == "win32" else ["m"],
        language="c",
    ),
]


setup(
    name="leekwars_c",
    version="0.1.0",
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
