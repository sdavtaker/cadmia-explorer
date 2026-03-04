# cadmia-explorer Implementation Plan

## Background

cadmia-explorer is a visualization tool for simulation logs produced by **cadpya**, an IA-DEVS
(Interval-Approximated DEVS) simulator. Each simulation run produces a JSONL log describing how
components (atomic and coupled models) evolve across branching simulation trajectories. The
visualizer renders these logs as per-component branching-tree plots.

See `reference.md` for IA-DEVS theory and `log-format.md` for the log format specification.


## Architecture Overview

```
JSONL log → [Python Preprocessor] → .cadvis binary → [C++ cadvis] → SVG files (Phase 1)
                                                                  ↘ [Phase 2] ImGui/OpenGL viewer
                                                                  ↘ [Phase 3] WebGL2 via Emscripten
```

The preprocessor does all data transformation (parsing, branch tree construction, edge
computation, multiplicity detection, output mapping). The C++ renderer is a pure
read → layout → draw pipeline with no runtime JSONL parsing.


## Visual Semantics

Each component gets its own plot. A plot shows:

- **Rectangles**: one per unique (time interval, output interval) pair for that component.
  - X axis = simulation time (interval lower/upper bound = left/right of rect)
  - Y axis = output value (interval lower/upper bound = bottom/top of rect)
  - Border style: **solid** = closed interval end, **dashed** = open interval end
    (each of the 4 sides is independently solid or dashed)
  - **Multiplicity**: when N branches produce the same (time, output) for this component,
    they collapse to one rect with "×N" in the corner
- **Edges**: directed lines connecting consecutive events in the branch tree
  - Sequential edges: consecutive visible events for this component within the same branch
  - Fan-out edges: last visible event in parent branch → first visible event in each child branch
  - Null-output entries, other-component entries, and skip branches are invisible but traversed
    for edge routing
- **Null-output entries**: NOT rendered (IC routing artifacts). Used only for edge routing.
- **Components with zero non-null outputs**: omitted from output entirely.


## Output Mapping

For compound string outputs (e.g. `"[j1:1, j1:1]"`), users provide a Python file with:

```python
def map_output(s: str) -> int:
    ...
```

The integer is used for Y-axis layout; the original string appears as the axis label.
If no mapping is provided, output intervals are parsed as numeric floats directly.


## Project Structure

```
cadmia-explorer/
├── preprocess/
│   ├── preprocess.py      # CLI: JSONL → .cadvis
│   ├── interval.py        # Interval string parser + Interval dataclass
│   └── cadvis_writer.py   # Binary format (.cadvis) writer
├── src/
│   ├── main.cpp           # CLI: .cadvis → SVG(s)
│   ├── reader.h / reader.cpp    # Binary reader → in-memory structs
│   ├── layout.h / layout.cpp    # Bounding box, coordinate mapping
│   └── svg.h / svg.cpp          # SVG string builder
├── CMakeLists.txt
├── test_logs/
│   ├── 4gp_log.jsonl      # 4 generators + 1 processor
│   ├── counter_log.jsonl  # FastGen + SlowGen + Counter
│   └── job_tracker_log.jsonl  # 4 generators + processor + 4 scorers
├── PLAN.md                # This file
├── CLAUDE.md              # AI-assistant project context
├── README.md              # User-facing overview
├── reference.md           # IA-DEVS theory background
├── log-format.md          # Simulator log format specification
└── prompt.md              # Original (superseded) project prompt
```

Phase 2+ adds:
```
src/
├── app.cpp          # ImGui application loop (desktop + WASM)
└── renderer.h/cpp   # OpenGL/DrawList rendering
```


## Phase 1: Python Preprocessor

### CLI

```
pipenv run python preprocess/preprocess.py \
    --log path/to/sim.jsonl \
    [--map path/to/map_output.py] \
    --output path/to/out.cadvis
```

### Processing Steps

**1. Parse JSONL**

Read all lines. Required fields: `branch`, `parent_branch`, `step`, `time`, `component`,
`kind`, `output`.

**2. Build branch tree**

```python
children_map    = defaultdict(list)   # branch_id → [child_branch_id, ...]
parent_map      = {}                  # branch_id → parent_branch_id | None
branch_entries  = defaultdict(list)   # branch_id → [entry, ...] sorted by step
```

Skip entries (`kind == "skip"`) are included in the tree structure but never contribute
visible events.

**3. Discover components**

All unique non-empty component names. Components with zero non-null outputs across the entire
log are silently omitted from output.

**4a. Collect visible events**

For each component C: entries where `component == C` and `output is not None`.

**4b. Parse interval strings** (`interval.py`)

Parses `[lo, hi]`, `[lo, hi)`, `(lo, hi]`, `(lo, hi)` into:

```python
@dataclass
class Interval:
    lo: float
    hi: float
    lo_closed: bool
    hi_closed: bool
```

Regex: `^([\[\(])\s*([-\d.e+]+)\s*,\s*([-\d.e+]+)\s*([\]\)])$`

For time: always numeric. For output: numeric unless `--map` is provided.

**4c. Apply output mapping**

If `--map` is given, load the user's Python file and call `map_output(raw_string) → int`.
Treat the result as a punctual interval `[n, n]` (closed both sides).
Store the original raw string as the Y-axis label.

If `--map` is not given, parse the output string as a numeric interval directly.
Use the raw string as the label.

**4d. Multiplicity detection**

Group visible events by `(time_raw_string, output_raw_string)`. Multiple entries sharing the
same key collapse to one rect with `multiplicity = count`. All N entries map to the same
rect index.

**4e. Edge algorithm**

```python
def compute_edges(C, branch_entries, parent_map, rect_from_entry):
    # branch_c_events: branch_id → sorted list of C-visible entries
    branch_c_events = {
        b: sorted([e for e in entries
                   if e["component"] == C and e["output"] is not None],
                  key=lambda e: e["step"])
        for b, entries in branch_entries.items()
        if any(e["component"] == C and e["output"] is not None
               for e in entries)
    }

    def last_in_lineage(branch_id):
        """Last C-visible event in branch_id or any ancestor."""
        if branch_id in branch_c_events:
            return branch_c_events[branch_id][-1]
        parent = parent_map.get(branch_id)
        if parent is not None:
            return last_in_lineage(parent)
        return None

    edges = set()
    for branch_id, events in branch_c_events.items():
        # Sequential edges within branch
        for i in range(1, len(events)):
            a, b = rect_from_entry(events[i-1]), rect_from_entry(events[i])
            if a != b:
                edges.add((a, b))
        # Incoming edge from nearest ancestor with a C-visible event
        parent = parent_map.get(branch_id)
        if parent is None:
            continue
        ancestor = last_in_lineage(parent)
        if ancestor is not None:
            a, b = rect_from_entry(ancestor), rect_from_entry(events[0])
            if a != b:
                edges.add((a, b))
    return list(edges)
```

Properties:
- Edges only span ancestor→descendant, never sibling→sibling
- Self-loops (from multiplicity merging) are filtered
- Edges are deduplicated via set


## Binary Format: `.cadvis`

All integers little-endian.

```
Header (12 bytes):
  [0:4]   magic         = "CADV"
  [4:6]   version       = uint16 = 1
  [6:8]   flags         = uint16 = 0  (reserved)
  [8:12]  n_components  = uint32

String pool:
  [0:4]   n_strings = uint32
  For each string:
    [0:4]   length = uint32
    [4:N]   utf-8 bytes  (no null terminator)

Component records (n_components entries):
  [0:4]   name_idx   = uint32  (string pool index)
  [4:8]   n_rects    = uint32
  [8:12]  n_edges    = uint32

  Rects  (n_rects × 48 bytes each):
    [0:8]   time_lo      = float64
    [8:16]  time_hi      = float64
    [16:24] out_lo       = float64  (mapped int cast to f64, or float)
    [24:32] out_hi       = float64
    [32:36] label_idx    = uint32   (string pool: original output string)
    [36:40] multiplicity = uint32
    [40:41] flags        = uint8
              bit 0 = time_lo_closed
              bit 1 = time_hi_closed
              bit 2 = out_lo_closed
              bit 3 = out_hi_closed
    [41:48] _padding     = 7 × 0x00

  Edges  (n_edges × 8 bytes each):
    [0:4]   from_idx = uint32  (index into this component's rect array)
    [4:8]   to_idx   = uint32
```


## Phase 1: C++ SVG Renderer

### CLI

```
cadvis [--component NAME | --all] [--output-dir DIR] <file.cadvis>
```

Default: `--all`, output directory = `.`. Produces `<component_name>.svg` per component.

### Data Structures (`reader.h`)

```cpp
struct Rect {
    double time_lo, time_hi;
    double out_lo,  out_hi;
    bool time_lo_closed, time_hi_closed;
    bool out_lo_closed,  out_hi_closed;
    std::string out_label;
    uint32_t multiplicity;
};
struct Edge  { uint32_t from, to; };
struct Component { std::string name; std::vector<Rect> rects; std::vector<Edge> edges; };
struct CadvisFile { std::vector<Component> components; };
```

### Layout (`layout.h/cpp`)

Canvas: **1200 × 900 px**
Margins: left = 80, right = 20, top = 30, bottom = 60
Plot area: 1100 × 810 px

```cpp
struct Scale {
    double t_min, t_max, y_min, y_max;
    double plot_x, plot_y, plot_w, plot_h;
    double time_to_px(double t) const;
    double out_to_py(double y) const;  // y inverted (high out → low py)
};
```

Enforce minimum rect pixel size: max(px_width, 2.0), max(px_height, 2.0).

### SVG Builder (`svg.h/cpp`)

No external library — pure string formatting.

**Rectangle borders**: each of the 4 sides drawn as a separate `<line>`:
- Closed end → `stroke-dasharray` omitted (solid)
- Open end → `stroke-dasharray="4,3"` (dashed)

The sides:
- Left  (`time_lo_closed`) — X = x1 side
- Right (`time_hi_closed`) — X = x2 side
- Bottom (`out_lo_closed`) — Y = y2 side (lower output = higher py due to inversion)
- Top   (`out_hi_closed`) — Y = y1 side

**Rectangle fill**: `<rect>` with `fill="rgba(70,130,180,0.3)"`.

**Multiplicity label**: `<text>` at top-right corner of rect: `×N` when `multiplicity > 1`.

**Edges**:
- Source point: center-right of source rect
- Target point: center-left of target rect
- Straight `<line>` with `marker-end="url(#arrow)"` for arrowhead
- When source and target x-centers differ by < 10px: cubic bezier with 40px horizontal offset
- Style: `stroke="#888"`, `opacity="0.5"`

**Axes**:
- X axis: ~8 ticks, labels to 3 decimal places
- Y axis: one tick per unique `out_lo` value, label = `out_label` string

**Build**:
```cmake
add_executable(cadvis
    src/main.cpp src/reader.cpp src/layout.cpp src/svg.cpp)
set_target_properties(cadvis PROPERTIES CXX_STANDARD 23)
```
No external C++ dependencies for Phase 1.


## Phase 2: Dear ImGui Interactive Viewer

After Phase 1 is validated on all test logs.

**Technology**: Dear ImGui (docking branch) + GLFW + OpenGL3 for desktop.
Same code compiled with Emscripten for web (GLFW replaced by emscripten browser integration).

**Features**:
- Sidebar: component dropdown, branch path display
- Custom OpenGL rendering via `ImDrawList` (`AddRectFilled`, `AddRect`, `AddLine`, `AddBezierCubic`)
- Pan (drag) and zoom (scroll wheel)
- Click rect → highlight prefix path (all ancestor rects brightened, others dimmed)

**Phase 2 additions to CMake**: detect `EMSCRIPTEN`, link `USE_WEBGL2=1 USE_GLFW=3`.


## Phase 3: WebGL2 Scale Optimization

For GB-scale logs with thousands of visible rects:
- Raw OpenGL VAO/VBO with instanced rendering (`glDrawArraysInstanced`)
- Viewport culling in vertex shader
- This is the WebGL2 instanced rendering from the original prompt


## Implementation Order

### Milestone 1 — Python preprocessor
1. `interval.py`: parser + unit tests (open/closed, negatives, edge cases)
2. JSONL parser + branch tree builder
3. Per-component visible event extraction + output mapping hook
4. Multiplicity detection
5. Edge algorithm
6. `cadvis_writer.py`: binary format writer
7. `preprocess.py`: CLI glue + end-to-end test on all 3 test logs

### Milestone 2 — C++ SVG renderer
1. `reader.cpp`: binary reader, validate round-trip with Python output
2. `layout.cpp`: bounding box + scale computation
3. `svg.cpp`: rectangles with solid/dashed borders + fills
4. `svg.cpp`: edges with arrowheads
5. `svg.cpp`: axes, labels, multiplicity counts, title
6. `main.cpp`: CLI, `--all` / `--component` / `--output-dir`
7. End-to-end: generate SVGs for all 3 test logs and review visually

### Milestone 3 — Phase 2 ImGui (future)
### Milestone 4 — Phase 3 WebGL2 scale (future)


## Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Preprocessing language | Python | Flexible, no build step, easy user function hooks |
| Intermediate format | Custom binary (.cadvis) | Fast C++ reads, no JSON parsing in renderer |
| Phase 1 output | SVG | Browser-viewable, zero C++ dependencies |
| Phase 2 GUI | Dear ImGui + OpenGL3 | Single codebase for desktop + WASM |
| Null outputs | Not rendered; used for edge routing only | IC artifacts, not observable behavior |
| All-null components | Omitted from output | Nothing to show (expected for internal-only components) |
| Multiplicity | Count label on merged rect | Spatial position must preserve simulation semantics |
| Open/closed intervals | Per-side dashed/solid border | Encodes the mathematical interval meaning |
| Edge routing | Straight lines / beziers, low opacity | Sufficient for Phase 1 |
| Technology stack | C++23 + Emscripten | Single codebase for desktop and web |
