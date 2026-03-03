# Project Implementation Prompt

You are tasked with designing and implementing a high-performance, browser-based visualization system for large log-derived datasets.

This system must support:

- Millions of total elements
- Thousands of visible elements at a time
- Multiple synchronized plots (10–20 views)
- Smooth scrolling and zooming
- Interactive rectangle selection
- Prefix-based path filtering
- WebGL2 rendering
- Efficient windowed data handling

The system must be production-grade, modular, and scalable.

---

# 1. High-Level Goal

Build a multi-view, synchronized 2D visualization system that:

- Displays rectangles derived from log spans
- Draws directed edges (no arrowheads required)
- Supports prefix-based selection filtering
- Only renders visible window subsets
- Synchronizes scroll and filter state across multiple plots
- Maintains high performance at scale

---

# 2. Core Architecture

The implementation must follow this architectural separation:

## A. Data Preprocessing (Offline Step)

Preprocess logs into structured JSON containing:

- `id`
- `start_time`
- `end_time`
- `prefix_id`
- `entry`
- `exit`
- color metadata
- edge relationships

### Prefix Indexing Requirement

Build a prefix trie from span prefixes.

Assign each prefix:
- `prefix_id`
- `entry` and `exit` interval indices (DFS numbering)

This enables subtree membership testing:

```
node belongs to selected prefix if:
node.entry >= selected.entry AND node.exit <= selected.exit
```

All spans must be sorted by `start_time` for windowing.

---

## B. Shared State Store

Implement a global reactive state store (Zustand or Redux recommended).

State must contain:

```
{
  visibleStart: number,
  visibleEnd: number,
  selectedPrefixInterval: { entry: number, exit: number } | null,
  filters: object
}
```

All plot renderers must subscribe to this store.

---

## C. Rendering Engine

Use WebGL2.

### Important Constraint:
Use a single WebGL context with multiple viewports if possible.
Avoid creating 10–20 independent WebGL contexts.

---

# 3. Rendering Requirements

## A. Rectangles

Use instanced rendering.

Each rectangle instance must contain:

- center (vec2)
- size (vec2)
- fillColor (vec4)
- borderColor (vec4)
- selectedMask (float)
- borderMode (float)  // 0 = pale/dashed, 1 = strong/solid

### Shader Responsibilities

Fragment shader must:

- Render fill
- Render border without extra geometry
- Apply grey-out when selectedMask == 0
- Support dashed border mode (procedural in shader)

No duplicate geometry allowed for borders.

---

## B. Edges

Edges are simple line segments.

- Batched into a single vertex buffer
- No arrowheads required
- Each edge must also receive selectedMask

Edge is active only if both endpoints belong to selected prefix interval.

---

## C. GPU Picking

Implement color-based picking:

1. Render to offscreen framebuffer
2. Encode instance ID as color
3. On click:
   - Read pixel
   - Decode ID
   - Dispatch selection event

Must be performant and precise.

---

# 4. Windowing Strategy

At runtime:

- Maintain visibleStart and visibleEnd
- Use binary search on sorted spans
- Extract visible slice
- Upload only visible slice to GPU buffers

Never upload all spans at once.

Windowing must support millions of total spans.

---

# 5. Multi-Plot Synchronization

All plots must:

- Share visibleStart and visibleEnd
- Share selectedPrefixInterval
- React to updates immediately

Scrolling in one plot must update global visible range.

Selection in one plot must update global selection state.

All plots must redraw accordingly.

Synchronization must operate at data-domain level (not pixel scroll).

---

# 6. Interaction Rules

When user clicks a rectangle:

1. Identify its prefix interval
2. Store in shared state
3. All visible rectangles:
   - If inside interval → selectedMask = 1
   - Else → selectedMask = 0
4. Update edge masks similarly

Selection logic must be O(K), where K is visible elements only.

---

# 7. Folder Structure

Implement project using this structure:

```
/data
  preprocess/
    buildPrefixIndex.ts
    parseLogs.ts

/src
  /core
    dataStore.ts
    windowing.ts
    prefixLogic.ts

  /render
    WebGLRenderer.ts
    shaders/
      rect.vert
      rect.frag
      edge.vert
      edge.frag

  /ui
    App.tsx
    PlotCanvas.tsx
    Controls.tsx
```

Use TypeScript.

Use Vite for build tooling.

---

# 8. Performance Requirements

System must:

- Maintain 60 FPS with 3,000–10,000 visible elements
- Support millions of total spans in memory
- Perform O(log N) window lookup
- Perform O(K) selection update
- Avoid excessive GPU buffer reallocations

Buffers should be reused when possible.

---

# 9. Milestones

Implement in phases:

### Phase 1
- Log preprocessing
- Prefix trie + interval encoding
- Sorted span export

### Phase 2
- Basic WebGL2 instanced rectangle renderer

### Phase 3
- Windowing logic
- Visible slice rendering

### Phase 4
- GPU picking
- Selection masking

### Phase 5
- Multi-plot synchronization

### Phase 6
- Optimization & performance tuning

---

# 10. Constraints

- No SVG-based rendering
- No D3 rendering pipeline
- No Qt
- No Canvas 2D fallback
- Must use WebGL2
- Must use instanced rendering
- Must use centralized state synchronization

---

# 11. Deliverables

- Working interactive prototype
- Clean modular code
- Clear separation of:
  - Data
  - State
  - Rendering
  - UI
- Documented shaders
- Performance notes

---

# 12. Implementation Philosophy

- Prefer correctness and clarity before micro-optimization
- Avoid premature C++ or WASM unless profiling demands it
- Keep rendering logic independent from React lifecycle
- Keep data immutable where practical
- Ensure architecture can scale further if needed

---

End of specification.
