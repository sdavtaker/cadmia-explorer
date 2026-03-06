#!/usr/bin/env bash
# Configure and build the C++ targets (cadvis + cadvis-test).
# Requires vcpkg: set VCPKG_ROOT or VCPKG_INSTALLATION_ROOT in environment.
# Writes build artifacts to ./build/.
# Exit code 0 = success, non-zero = build error.
set -euo pipefail
cd "$(dirname "$0")/.."

VCPKG_TC="${VCPKG_ROOT:-${VCPKG_INSTALLATION_ROOT:-}}/scripts/buildsystems/vcpkg.cmake"
if [[ ! -f "$VCPKG_TC" ]]; then
    echo "ERROR: vcpkg toolchain not found at $VCPKG_TC"
    echo "Set VCPKG_ROOT or VCPKG_INSTALLATION_ROOT to your vcpkg installation."
    exit 1
fi

# Use system cmake/ninja rather than letting vcpkg download its own copies.
# Prevents "Host path separator" errors on Linux and speeds up first-time builds.
export VCPKG_FORCE_SYSTEM_BINARIES=1

# Extra cmake args passed through (e.g. -DBUILD_GUI=OFF for headless-only builds)
EXTRA_ARGS=("$@")

cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TC" \
    "${EXTRA_ARGS[@]}"
cmake --build build --parallel
