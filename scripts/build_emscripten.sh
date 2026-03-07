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

# Copy shell.html into the build directory as index.html so it can be served
# alongside the generated cadvis.js / cadvis.wasm.
cp shell.html build-wasm/index.html

echo ""
echo "Build complete: build-wasm/cadvis.js + cadvis.wasm"
echo ""
echo "To serve locally:"
echo "  python3 -m http.server 8080 --directory build-wasm"
echo "  open http://localhost:8080"
