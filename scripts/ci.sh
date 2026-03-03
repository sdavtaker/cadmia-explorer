#!/usr/bin/env bash
# Run the full CI suite locally: lint, unit tests, build, e2e tests.
# This mirrors exactly what the GitHub Actions CI runs.
# Exit code 0 = everything passed.
set -euo pipefail
cd "$(dirname "$0")/.."

echo ">>> lint_python"
./scripts/lint_python.sh

echo ">>> test_python"
./scripts/test_python.sh

echo ">>> build_cpp"
./scripts/build_cpp.sh

echo ">>> test_e2e"
./scripts/test_e2e.sh

echo ""
echo "All CI checks passed."
