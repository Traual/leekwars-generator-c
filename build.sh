#!/usr/bin/env bash
# Linux/WSL build script. CMake + Ninja, runs the C-level test suite.
set -euo pipefail

cd "$(dirname "$0")"

if ! command -v cmake >/dev/null; then
    echo "ERROR: cmake not installed. apt-get install cmake ninja-build"
    exit 1
fi

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
