"""Build the v2 Python bindings against the line-by-line Java port.

The v2 engine lives in src_v2/ + include_v2/.  This setup compiles
every src_v2/lw_*.c into a single C extension named ``leekwars_c_v2._engine``.

Run from the repo root:

    python bindings/python/setup_v2.py build_ext --inplace
"""
import glob
import os
import sys

from setuptools import setup, Extension
from Cython.Build import cythonize


HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


C_SOURCES = sorted(glob.glob(os.path.join(ROOT, "src_v2", "lw_*.c")))

INCLUDE_DIRS = [
    os.path.join(ROOT, "include_v2"),
]


extensions = [
    Extension(
        name="leekwars_c_v2._engine",
        sources=[
            os.path.join(HERE, "leekwars_c_v2", "_engine.pyx"),
            *C_SOURCES,
        ],
        include_dirs=INCLUDE_DIRS,
        extra_compile_args=["/O2"] if sys.platform == "win32" else ["-O2"],
        libraries=[] if sys.platform == "win32" else ["m"],
        language="c",
    ),
]


setup(
    name="leekwars_c_v2",
    version="0.0.1",
    packages=["leekwars_c_v2"],
    package_dir={"leekwars_c_v2": os.path.join(HERE, "leekwars_c_v2")},
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
