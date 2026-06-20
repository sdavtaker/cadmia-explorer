# cadmia-explorer — AI Assistant Context

This file provides context for AI-assisted development on the cadmia-explorer project.

## What This Project Is

A visualization tool for simulation logs produced by **cadpya**, a Python IA-DEVS
(Interval-Approximated DEVS) simulator. The simulator outputs JSONL logs with branching
simulation trajectories; this tool renders them as per-component branching-tree plots.

**Do not modify** `reference.md` or `log-format.md` without confirming with the user — these
are ground-truth specifications derived from the simulator source and may be updated when the
simulator is updated.

## Technology Stack

- **Preprocessor**: Python 3 (no special dependencies beyond stdlib). Located in `preprocess/`.
- **Renderer (Phase 1)**: C++23, no external dependencies. Outputs SVG. Located in `src/`.
- **Renderer (Phase 2+)**: C++23 + Dear ImGui + GLFW + OpenGL3. Same code via Emscripten for WASM.
- **Build system**: CMake (minimum 3.20) + **vcpkg** (manifest mode) for C++ dependencies.
- **C++ dependencies**: managed via `vcpkg.json`; pass `-DCMAKE_TOOLCHAIN_FILE` to cmake.
  `build_cpp.sh` reads `$VCPKG_ROOT` or `$VCPKG_INSTALLATION_ROOT` automatically.
- **No JavaScript/TypeScript** — the original `prompt.md` specified a React/TS stack; this was
  superseded. Ignore the technology choices in `prompt.md`.

## Project Status

- [x] Requirements clarified (see `PLAN.md`)
- [x] Log format confirmed (see `log-format.md`)
- [x] Test logs available in `test_logs/`
- [x] Phase 1 complete — Python preprocessor + C++ SVG renderer
- [ ] **In progress**: Phase 2 — Dear ImGui + GLFW + OpenGL3 interactive viewer

## File Map

| File | Purpose |
|---|---|
| `PLAN.md` | **Primary reference** — full implementation plan, all design decisions |
| `reference.md` | IA-DEVS theory background (do not modify without user confirmation) |
| `log-format.md` | Simulator JSONL log format spec (do not modify without user confirmation) |
| `prompt.md` | Original/superseded spec (kept for historical reference only) |
| `preprocess/` | Python preprocessor source |
| `src/` | C++ renderer source |
| `test_logs/` | Small JSONL logs for development/testing |

## Key Conventions

### Log Format

Seven fields per JSONL entry: `branch`, `parent_branch`, `step`, `time`, `component`, `kind`, `output`.

- `kind`: `"atomic"` | `"coupled"` | `"skip"`
- `output`: string interval like `"[1.997, 2.003]"` or compound `"[j1:1, j1:1]"`, or `null`
- `null` output = IC (internal coupling) routing artifact — **not visualized**, but used for edge routing
- Skip entries (`kind == "skip"`, `component == ""`) — **not visualized**, used for branch tree structure only

### Interval Strings

Format: `[lo, hi]`, `[lo, hi)`, `(lo, hi]`, `(lo, hi)` where `[`/`]` = closed, `(`/`)` = open.
Time intervals always numeric. Output intervals numeric unless user provides `--map` script.

### Branch Tree

Branch IDs are monotonic integers as strings: `"0"` (root), `"1"`, `"2"`, etc.
The root branch `"0"` may not appear in the log if branching happens immediately.
Every non-root branch ID is guaranteed to appear in the log.
Dedup entries (`kind: "dedup"`) carry a `merged_into` field and are excluded from the active branch tree.

### Binary Format

The `.cadvis` binary format is specified precisely in `PLAN.md` § "Binary Format: .cadvis".
It is an internal format — not intended for human consumption.

### Edge Computation

For component C's plot, edges connect visible (non-null) C events in the branch tree:
- **Sequential**: consecutive C-visible events within the same branch
- **Fan-out**: last C-visible event in a branch → first C-visible event in each child branch
- **Merge**: last C-visible event in a dedup'd branch → first C-visible event in the surviving branch
- Traverses transparently through null-output, other-component, skip, and dedup entries
- Self-loops (from multiplicity merging) are filtered out

### SVG Conventions

- Canvas: 1200 × 900 px, margins: left=80, right=20, top=30, bottom=60
- Open interval bounds → dashed border on that side
- Closed interval bounds → solid border on that side
- Multiplicity > 1 → `×N` label in top-right corner of rect
- Edge opacity: 0.5 (to reduce clutter)

## Output Mapping

For compound string outputs, users provide a Python file with `def map_output(s: str) -> int`.
Pass it via `--map path/to/script.py` to the preprocessor. The integer is used for Y-layout;
the original string is shown on the Y axis.

## Test Logs

| Log | Components | Notes |
|---|---|---|
| `4gp_log.jsonl` | G1, G2, G3, G4, P | Generators have non-null output `[1.997, 2.003]`; P has null output (routes internally) |
| `counter_log.jsonl` | FastGen, SlowGen, Counter | Generators are null-output (IC only); Counter has integer outputs |
| `job_tracker_log.jsonl` | G1-G4, P, S1-S4 | G1-G4 and P null; S1-S4 have compound string outputs like `"[j1:1, j1:1]"` |

## Validation Checklist

Run this before every commit and before marking any task complete:

```bash
./scripts/lint_python.sh   # ruff: must be clean (0 errors)
./scripts/test_python.sh   # pytest: all 43+ tests must pass
./scripts/build_cpp.sh     # cmake build: must succeed with no errors
./scripts/lint_cpp.sh      # clang-format + clang-tidy: must be clean (0 errors)
./scripts/test_cpp.sh      # C++ unit tests (Catch2/CTest): all must pass
./scripts/test_e2e.sh      # end-to-end pipeline: all SVG checks must pass
```

Or run the full suite at once:

```bash
./scripts/ci.sh
```

The CI workflow (`.github/workflows/ci.yml`) runs these same scripts on every push and PR.
**Do not commit if any script fails.**

## Python Environment

**Always use pipenv** — never invoke `python` or `pip` directly.

```bash
# Install / sync dependencies (first time or after Pipfile changes)
pipenv install

# Run a script
pipenv run python preprocess/preprocess.py --log test_logs/4gp_log.jsonl --output out.cadvis

# Run tests
pipenv run pytest preprocess/

# Add a new dependency
pipenv install <package>

# Add a dev-only dependency
pipenv install --dev <package>
```

## Commands

```bash
# Preprocess a log
pipenv run python preprocess/preprocess.py --log test_logs/4gp_log.jsonl --output out.cadvis

# Preprocess with output mapping
pipenv run python preprocess/preprocess.py --log test_logs/job_tracker_log.jsonl \
    --map examples/job_map.py --output out.cadvis

# Render SVGs (all components)
./build/cadvis --all --output-dir svgs/ out.cadvis

# Render SVGs (one component)
./build/cadvis --component G1 --output-dir svgs/ out.cadvis

# Build C++ (desktop)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Build WASM (requires emsdk sourced)
./scripts/build_emscripten.sh

# Serve WASM locally
python3 -m http.server 8080 --directory build-wasm
# open http://localhost:8080
```
