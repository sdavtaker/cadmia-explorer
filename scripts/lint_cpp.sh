#!/usr/bin/env bash
# C++ style and static analysis checks.
# Requires: clang-format, clang-tidy, and a pre-existing build/ directory
# containing compile_commands.json (run build_cpp.sh first).
set -euo pipefail
cd "$(dirname "$0")/.."

SRCS=(src/*.cpp src/*.h tests/*.cpp)

echo "=== clang-format ==="
clang-format --dry-run --Werror "${SRCS[@]}"
echo "  OK: formatting clean"

echo "=== clang-tidy ==="
if [[ ! -f build/compile_commands.json ]]; then
  echo "ERROR: build/compile_commands.json not found. Run ./scripts/build_cpp.sh first."
  exit 1
fi
clang-tidy -p build "${SRCS[@]}"
echo "  OK: tidy clean"
