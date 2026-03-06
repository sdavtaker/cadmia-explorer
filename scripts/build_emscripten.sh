#!/usr/bin/env bash
# Build cadmia-explorer as a WebAssembly application using Emscripten.
# Prerequisites:
#   - emsdk installed and activated: source /path/to/emsdk/emsdk_env.sh
#   - cmake 3.20+, git, internet access (FetchContent downloads imgui on first build)
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v emcmake &>/dev/null; then
  echo "ERROR: emcmake not found."
  echo "  Install emsdk and run:  source /path/to/emsdk/emsdk_env.sh"
  exit 1
fi

emcmake cmake -B build-wasm \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_GUI=ON \
  "$@"

cmake --build build-wasm --parallel

echo ""
echo "Build complete: build-wasm/cadvis.html"
echo ""
echo "To serve locally:"
echo "  python3 -m http.server 8080 --directory build-wasm"
echo "  open http://localhost:8080/cadvis.html"
