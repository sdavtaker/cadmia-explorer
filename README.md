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

## Quick Start (Phase 1: SVG Output)

**Prerequisites**: Python 3.8+, CMake 3.20+, a C++23 compiler.

```bash
# 1. Preprocess a log into the intermediate binary format
python preprocess/preprocess.py \
    --log test_logs/4gp_log.jsonl \
    --output out.cadvis

# 2. Build the C++ SVG renderer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3. Render SVG files (one per component)
./build/cadvis --all --output-dir svgs/ out.cadvis
```

Open any `.svg` file in a browser to view the plot.

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
python preprocess/preprocess.py \
    --log test_logs/job_tracker_log.jsonl \
    --map my_map.py \
    --output out.cadvis
```

## Roadmap

| Phase | Status | Description |
|---|---|---|
| 1 | In progress | Python preprocessor + C++ SVG renderer |
| 2 | Planned | Interactive Dear ImGui viewer (desktop + WASM) |
| 3 | Planned | WebGL2 instanced rendering for GB-scale logs |

## Project Layout

```
preprocess/          Python preprocessor (JSONL → .cadvis binary)
src/                 C++ renderer source
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
