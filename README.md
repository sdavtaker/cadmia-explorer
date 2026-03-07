# cadmia-explorer

Visualization tools for [cadpya](https://github.com/cadmia-project/cadpya) IA-DEVS simulation logs.

## What It Does

cadpya simulates systems modeled as IA-DEVS (Interval-Approximated Discrete Event System
Specification). Simulations produce JSONL logs describing how each component transitions
through interval-valued states across a branching tree of possible trajectories.

cadmia-explorer turns those logs into visual plots — one per component — showing:

- **Rectangles**: each event represented as a rectangle where X = simulation time interval,
  Y = output value interval. Border style (solid/dashed) encodes closed/open interval bounds.
- **Edges**: arrows connecting sequential events within branches and fanning out at branch points.
- **Multiplicity**: when multiple branches produce the same event, a single rectangle with
  a count label (`×N`) represents them.

## Quick Start

**Prerequisites**: Python 3.12+, pipenv, CMake 3.20+, a C++23 compiler, vcpkg.

```bash
# 0. Set up the Python environment (first time only)
pipenv install

# 1. Preprocess a log into the intermediate binary format
pipenv run python preprocess/preprocess.py \
    --log test_logs/4gp_log.jsonl \
    --output out.cadvis

# 2. Build the C++ viewer
export VCPKG_ROOT=/path/to/vcpkg   # or VCPKG_INSTALLATION_ROOT
./scripts/build_cpp.sh

# 3a. Open the interactive viewer
./build/cadvis out.cadvis

# 3b. Or render static SVG files (headless, no display required)
./build/cadvis --all --output-dir svgs/ out.cadvis
```

### Interactive Viewer Controls

| Action | Control |
|---|---|
| Pan | Left-click drag |
| Zoom | Scroll wheel (zooms toward cursor) |
| Select rect | Left-click |
| Highlight ancestor path | Automatic on click — ancestors brighten, others dim |
| Cycle branch paths on a ×N rect | PgUp / PgDn |
| Deselect | Click empty canvas |

The **sidebar** shows:
- Component selector dropdown
- Selected rect details: time interval, output value, branch multiplicity
- `(N / M)` path indicator and `PgUp/PgDn` hint when multiple ancestor paths exist

### Building Without a Display (headless SVG only)

If you have no X11/display headers (e.g. a headless server):

```bash
./scripts/build_cpp.sh -DBUILD_GUI=OFF
./build/cadvis --all --output-dir svgs/ out.cadvis
```

Open any `.svg` file in a browser to view the plot.

### Browser Viewer (WebAssembly)

The viewer can also be compiled to WebAssembly and run in any modern browser — no install
required for end users.

**Prerequisites**: [emsdk](https://github.com/emscripten-core/emsdk) installed and activated.

```bash
# First-time emsdk setup (once per machine)
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest
~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh

# Build the WASM viewer
./scripts/build_emscripten.sh
```

This produces `build-wasm/cadvis.js`, `build-wasm/cadvis.wasm`, and `build-wasm/index.html`.

**Serve locally for testing** (browsers require a server — `file://` won't work due to CORS):

```bash
python3 -m http.server 8080 --directory build-wasm
```

Then open **http://localhost:8080** in your browser.

Once the canvas loads, click **"Open .cadvis..."** in the sidebar, select a `.cadvis` file from
disk, and the component dropdown and plot will populate. All interactive controls (pan, zoom,
click, PgUp/PgDn) work identically to the desktop viewer.

**To share with others**, copy the three files (`index.html`, `cadvis.js`, `cadvis.wasm`) to
any static web host (GitHub Pages, nginx, S3, etc.) — no server-side logic required.

### Compound Output Mapping

For models whose outputs are compound strings (e.g. `"[j1:1, j1:1]"`), provide a Python
mapping function that converts the string to an integer for Y-axis layout:

```python
# my_map.py
def map_output(s: str) -> int:
    # parse and return an integer
    ...
```

```bash
pipenv run python preprocess/preprocess.py \
    --log test_logs/job_tracker_log.jsonl \
    --map my_map.py \
    --output out.cadvis
```

## Roadmap

| Phase | Status | Description |
|---|---|---|
| 1 | Complete | Python preprocessor + C++ SVG renderer |
| 2 | Complete | Interactive Dear ImGui desktop viewer |
| 3a | Complete | Emscripten WASM port — browser viewer, no install required |
| 3b | Planned | GPU instanced rendering (glDrawArraysInstanced) for GB-scale logs |

## Project Layout

```
preprocess/          Python preprocessor (JSONL → .cadvis binary)
src/                 C++ viewer source
test_logs/           Small sample logs for development
PLAN.md              Full implementation plan and design decisions
reference.md         IA-DEVS theoretical background
log-format.md        Simulator log format specification
```

## Background

IA-DEVS extends classic DEVS simulation to propagate uncertainty through interval arithmetic.
Each simulation step produces an interval of possible outcomes rather than a single value,
and when multiple components are simultaneously imminent the simulator branches (BFS) to
explore all possible execution orderings.

See `reference.md` for the full theoretical background.
