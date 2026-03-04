#!/usr/bin/env bash
# Configure and build the C++ cadvis binary.
# Writes build artifacts to ./build/.
# Exit code 0 = success, non-zero = build error.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
