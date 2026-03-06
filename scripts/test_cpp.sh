#!/usr/bin/env bash
# Run C++ unit tests via CTest.
# Requires the project to have been built first (./scripts/build_cpp.sh).
# Exit code 0 = all tests passed, non-zero = failure.
set -euo pipefail
cd "$(dirname "$0")/.."

ctest --test-dir build --output-on-failure
