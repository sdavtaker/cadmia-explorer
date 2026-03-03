#!/usr/bin/env bash
# Lint all Python source with ruff.
# Exit code 0 = clean, non-zero = lint errors.
set -euo pipefail
cd "$(dirname "$0")/.."

pipenv run ruff check preprocess/ examples/ tests/
