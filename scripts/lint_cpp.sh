#!/usr/bin/env bash
# C++ style and static analysis checks.
# Requires: clang-format, clang-tidy, and a pre-existing build/ directory
# containing compile_commands.json (run build_cpp.sh first).
set -euo pipefail
cd "$(dirname "$0")/.."

ALL_SRCS=(src/*.cpp src/*.h tests/*.cpp)
CPP_SRCS=(src/*.cpp tests/*.cpp)

echo "=== clang-format ==="
clang-format --dry-run --Werror "${ALL_SRCS[@]}"
echo "  OK: formatting clean"

echo "=== clang-tidy ==="
if [[ ! -f build/compile_commands.json ]]; then
  echo "ERROR: build/compile_commands.json not found. Run ./scripts/build_cpp.sh first."
  exit 1
fi
# Only tidy files present in the compile database; BUILD_GUI=OFF builds omit
# app.cpp and renderer.cpp, which need imgui headers not available in that mode.
TIDY_SRCS=()
for f in "${CPP_SRCS[@]}"; do
  abs=$(realpath "$f")
  if grep -q "\"${abs}\"" build/compile_commands.json; then
    TIDY_SRCS+=("$f")
  fi
done
clang-tidy -p build "${TIDY_SRCS[@]}"
echo "  OK: tidy clean"
