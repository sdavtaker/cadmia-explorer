# cadpya Log Format Reference

This document describes the JSONL log format produced by `RootCoordinator.simulate()`. It is intended as a reference for the visualization project.

## Format

Newline-delimited JSON (JSONL). Each line is one JSON object representing a single simulation step.

## Fields

| Field | Type | Description |
|-------|------|-------------|
| `branch` | `string` | Hierarchical BFS tree ID. Root branch is `"0"`, children are `"0.0"`, `"0.1"`, etc. Deeper levels append further `.N` suffixes. |
| `component` | `string` | Name of the component that fired. Empty string `""` for skip entries. |
| `kind` | `string` | Component kind: `"atomic"` for atomic model components, `"coupled"` for nested coupled model components, or `"skip"` for skip branches (no component fires in this interval). |
| `output` | `string \| null` | External output coupling (EOC) value as an interval string, or `null` if the component's output was routed only internally (IC), not to the coupled model boundary. Always `null` for skip entries. |
| `parent_branch` | `string \| null` | Parent branch ID. `null` for entries on the root branch `"0"`. |
| `step` | `int` | Step number within this branch lineage. Starts at 0 and increments each time the branch advances. |
| `time` | `string` | Time interval when the event occurred, formatted as an interval string (e.g. `"[0.997, 1.005]"`). May use open bounds like `"[0.810, 0.950)"`. |

## Semantics

### BFS Branch Tree

The simulator uses breadth-first search to explore uncertainty. When multiple components are simultaneously imminent with punctual (single-point) time advances, the SELECT tie-breaking function picks one — but since the true execution order is uncertain, the simulator forks: one branch per candidate ordering.

- **No branching**: When only one component is imminent or time advances are non-punctual, the simulation proceeds linearly on the same branch.
- **Branching**: When N components are simultaneously imminent with punctual limits, N child branches are created. Each branch represents a different execution ordering.

### IC vs EOC Events

- **`output: null`**: The component fired and its output was routed to other components inside the coupled model (internal coupling, IC), but nothing reached the coupled model boundary (EOC). This is the common case for generators feeding processors.
- **`output: "[1, 1]"`**: The component's output was routed to `"self"` (the coupled model's external output port) via an EOC translation function.

### Skip Branches

When a non-punctual time interval contains multiple simultaneously imminent
components, the simulator also explores the possibility that *nothing* fires in
that interval (the event could happen later). These "skip" branches are logged
with `kind: "skip"`, `component: ""`, and `output: null`.

Every branch ID that is assigned appears in the log — including skip branches —
so `parent_branch` references are always resolvable. The only exception is the
root branch `"0"` itself, which may not appear as a log entry if branching
happens before any root-level component fires.

### Passive Branches

Branches where `t_next` becomes `None` (all components are passive with no scheduled events) are silently discarded. They do not appear in the log.

## Branch Tree Reconstruction

To build a parent-children tree from the flat log:

```python
import json
from collections import defaultdict

children = defaultdict(set)
with open("log.jsonl") as f:
    for line in f:
        entry = json.loads(line)
        parent = entry["parent_branch"]
        branch = entry["branch"]
        if parent is not None:
            children[parent].add(branch)
```

## Annotated Example

From the 4GP simulation (4 Generators + 1 Processor):

```jsonl
{"branch": "0.0", "component": "G1", "kind": "atomic", "output": null, "parent_branch": "0", "step": 0, "time": "[0.997, 1.005]"}
{"branch": "0.1", "component": "G2", "kind": "atomic", "output": null, "parent_branch": "0", "step": 0, "time": "[0.997, 1.005]"}
{"branch": "0.2", "component": "G3", "kind": "atomic", "output": null, "parent_branch": "0", "step": 0, "time": "[0.997, 1.005]"}
{"branch": "0.3", "component": "G4", "kind": "atomic", "output": null, "parent_branch": "0", "step": 0, "time": "[0.997, 1.005]"}
```

**Step 0**: All four generators are simultaneously imminent at time `[0.997, 1.005]`. The simulator creates 4 branches from the root `"0"`, one for each generator firing first. In branch `"0.0"`, G1 fires first; in `"0.1"`, G2 fires first; and so on. Output is `null` because generator output is routed internally to the processor (IC), not to the coupled model boundary. All entries have `kind: "atomic"` since generators are atomic models.

```jsonl
{"branch": "0.0.0.0", "component": "P", "kind": "atomic", "output": "[1, 1]", "parent_branch": "0.0.0", "step": 4, "time": "[1.223, 1.255]"}
```

**Step 4** (branch `0.0.0.0`): The processor finishes its first job and outputs `[1, 1]` (job ID 1) via EOC. The time interval `[1.223, 1.255]` reflects accumulated uncertainty from the generator periods and processing time.

Skip branch example (from the counter simulation):

```jsonl
{"branch": "0.0", "component": "FastGen", "kind": "atomic", "output": null, "parent_branch": "0", "step": 8, "time": "[0.810, 0.950)"}
{"branch": "0.1", "component": "", "kind": "skip", "output": null, "parent_branch": "0", "step": 8, "time": "[0.810, 0.950)"}
{"branch": "0.1.0", "component": "FastGen", "kind": "atomic", "output": null, "parent_branch": "0.1", "step": 9, "time": "[0.950, 0.990]"}
{"branch": "0.1.1", "component": "SlowGen", "kind": "atomic", "output": null, "parent_branch": "0.1", "step": 9, "time": "[0.950, 0.990]"}
```

**Step 8** (branching): FastGen and SlowGen have overlapping but non-punctual `t_next` intervals. The limit `[0.810, 0.950)` is strictly inside FastGen's `t_next`, so a skip branch `"0.1"` is created alongside the FireFastGen branch `"0.0"`. The skip branch records that no component fired in `[0.810, 0.950)` — SlowGen might fire instead at the next step. Both child branches `"0.1.0"` and `"0.1.1"` can safely reference `parent_branch: "0.1"`.
