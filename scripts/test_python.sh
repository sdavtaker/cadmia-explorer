#!/usr/bin/env bash
# Run the Python unit and end-to-end test suite with pytest.
# Exit code 0 = all passed, non-zero = failures.
set -euo pipefail
cd "$(dirname "$0")/.."

pipenv run pytest tests/ "$@"
