#!/usr/bin/env python3
"""
preprocess.py — Convert a cadpya JSONL simulation log to a .cadvis binary.

Usage:
  python preprocess.py --log SIM.jsonl [--map MAP.py] --output OUT.cadvis

Arguments:
  --log    PATH   Path to the JSONL simulation log.
  --map    PATH   Optional Python file defining map_output(s: str) -> int.
                  Used for compound string outputs (e.g. "[j1:1, j1:1]").
                  If omitted, output intervals are parsed as numeric floats.
  --output PATH   Output .cadvis file path.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from collections import defaultdict
from pathlib import Path
from typing import Callable, Optional

from cadvis_writer import CadvisWriter, ComponentRecord, EdgeRecord, RectRecord
from interval import parse_interval

# ---------------------------------------------------------------------------
# Output mapping
# ---------------------------------------------------------------------------

def load_map_function(path: str) -> Callable[[str], int]:
    """Load map_output(s) -> int from the given Python file."""
    spec = importlib.util.spec_from_file_location("user_map", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not hasattr(module, 'map_output'):
        raise ValueError(f"{path!r} must define a function named 'map_output'")
    return module.map_output


# ---------------------------------------------------------------------------
# Log parsing
# ---------------------------------------------------------------------------

def load_log(path: str) -> list[dict]:
    entries = []
    with open(path, encoding='utf-8') as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
            except json.JSONDecodeError as e:
                print(f"Warning: skipping malformed JSON at line {lineno}: {e}",
                      file=sys.stderr)
                continue
            entries.append(entry)
    return entries


# ---------------------------------------------------------------------------
# Branch tree construction
# ---------------------------------------------------------------------------

def build_branch_tree(entries: list[dict]) -> tuple[
    dict[str, list[dict]],    # branch_entries: branch_id -> [entry, ...] sorted by step
    dict[str, list[str]],     # children_map:   branch_id -> [child_branch_id, ...]
    dict[str, Optional[str]], # parent_map:     branch_id -> parent_branch_id | None
    dict[str, str],           # merge_map:      dedup'd branch_id -> surviving branch_id
]:
    # First pass: identify dedup'd branches so we can exclude them from children_map.
    # A dedup'd branch's future is subsumed by the surviving branch; it must not
    # appear as a regular child because the edge algorithm handles it separately.
    merge_map: dict[str, str] = {}
    for entry in entries:
        if entry.get('kind') == 'dedup':
            merge_map[entry['branch']] = entry['merged_into']

    branch_entries: dict[str, list[dict]] = defaultdict(list)
    children_map:   dict[str, list[str]]  = defaultdict(list)
    parent_map:     dict[str, Optional[str]] = {}
    seen_children:  dict[str, set[str]]   = defaultdict(set)

    for entry in entries:
        branch_id = entry['branch']
        parent_id = entry['parent_branch']

        branch_entries[branch_id].append(entry)
        parent_map[branch_id] = parent_id

        if (parent_id is not None
                and branch_id not in merge_map
                and branch_id not in seen_children[parent_id]):
            children_map[parent_id].append(branch_id)
            seen_children[parent_id].add(branch_id)

    # Sort each branch's entries by step
    for branch_id in branch_entries:
        branch_entries[branch_id].sort(key=lambda e: e['step'])

    return dict(branch_entries), dict(children_map), parent_map, merge_map


# ---------------------------------------------------------------------------
# Per-component processing
# ---------------------------------------------------------------------------

def discover_components(entries: list[dict]) -> list[str]:
    """Return sorted list of unique non-empty component names with non-null outputs."""
    with_output = set()
    for entry in entries:
        if entry.get('component') and entry.get('output') is not None:
            with_output.add(entry['component'])
    return sorted(with_output)


def parse_output(raw: str, map_fn: Optional[Callable[[str], int]]) -> tuple[float, float, bool, bool, str]:
    """
    Parse output string → (lo, hi, lo_closed, hi_closed, label).

    If map_fn is provided, call it and treat the result as a punctual [n, n] interval.
    Otherwise parse the string as a numeric interval.
    """
    label = raw
    if map_fn is not None:
        n = map_fn(raw)
        if not isinstance(n, int):
            raise TypeError(f"map_output({raw!r}) returned {type(n).__name__}, expected int")
        return float(n), float(n), True, True, label
    iv = parse_interval(raw)
    return iv.lo, iv.hi, iv.lo_closed, iv.hi_closed, label


# ---------------------------------------------------------------------------
# Multiplicity detection
# ---------------------------------------------------------------------------

def collect_visible_events(
    component: str,
    branch_entries: dict[str, list[dict]],
    map_fn: Optional[Callable[[str], int]],
) -> tuple[list[RectRecord], dict[int, int]]:
    """
    Gather all visible (non-null output) events for `component`.

    Returns:
      rects: deduplicated list of RectRecord (one per unique time+output pair)
      entry_to_rect: maps raw entry index (in all_events list) to rect index
    """
    # Collect all visible entries in order
    all_events: list[dict] = []
    for branch_id in sorted(branch_entries.keys()):
        for entry in branch_entries[branch_id]:
            if entry['component'] == component and entry['output'] is not None:
                all_events.append(entry)

    # Group by (time_raw, output_raw) key for multiplicity
    key_to_rect_idx: dict[tuple[str, str], int] = {}
    rect_counts: dict[int, int] = {}
    rects: list[RectRecord] = []
    entry_to_rect: dict[int, int] = {}

    for event_idx, entry in enumerate(all_events):
        time_raw   = entry['time']
        output_raw = entry['output']
        key = (time_raw, output_raw)

        if key not in key_to_rect_idx:
            t_iv = parse_interval(time_raw)
            out_lo, out_hi, out_lc, out_hc, label = parse_output(output_raw, map_fn)
            rect_idx = len(rects)
            rects.append(RectRecord(
                time_lo=t_iv.lo, time_hi=t_iv.hi,
                out_lo=out_lo, out_hi=out_hi,
                out_label=label,
                multiplicity=0,  # filled below
                time_lo_closed=t_iv.lo_closed, time_hi_closed=t_iv.hi_closed,
                out_lo_closed=out_lc, out_hi_closed=out_hc,
            ))
            key_to_rect_idx[key] = rect_idx
            rect_counts[rect_idx] = 0

        rect_idx = key_to_rect_idx[key]
        rect_counts[rect_idx] += 1
        entry_to_rect[event_idx] = rect_idx

    # Write back multiplicity counts
    for i, rect in enumerate(rects):
        rects[i] = RectRecord(
            time_lo=rect.time_lo, time_hi=rect.time_hi,
            out_lo=rect.out_lo, out_hi=rect.out_hi,
            out_label=rect.out_label,
            multiplicity=rect_counts[i],
            time_lo_closed=rect.time_lo_closed, time_hi_closed=rect.time_hi_closed,
            out_lo_closed=rect.out_lo_closed, out_hi_closed=rect.out_hi_closed,
        )

    # Build a per-branch mapping: branch_id -> [(entry, rect_idx), ...] sorted by step
    # We need this for the edge algorithm. Reconstruct from all_events + entry_to_rect.
    # Store the event_idx alongside so we can recover the entry.
    return rects, all_events, entry_to_rect


# ---------------------------------------------------------------------------
# Edge algorithm
# ---------------------------------------------------------------------------

def compute_edges(
    component: str,
    branch_entries: dict[str, list[dict]],
    parent_map: dict[str, Optional[str]],
    merge_map: dict[str, str],
    all_events: list[dict],
    entry_to_rect: dict[int, int],
) -> list[EdgeRecord]:
    """
    Compute edges for a component's plot.

    Rules:
    - Sequential edges: consecutive visible events within the same branch.
    - Fan-out edges: last visible event in an ancestor branch → first visible event
      in the current branch (traversing up through null-output branches).
    - Merge edges: last visible event in a dedup'd branch → first visible event in
      the surviving branch (stored as regular EdgeRecord; no format distinction).
    - Self-loops (same rect index) are filtered out.
    - Duplicate edges are deduplicated.
    """
    # Build a fast lookup: event_idx from (branch_id, step, component)
    # We need branch_id → sorted list of (event_idx, entry) for this component
    branch_c_visible: dict[str, list[tuple[int, dict]]] = defaultdict(list)
    for event_idx, entry in enumerate(all_events):
        branch_c_visible[entry['branch']].append((event_idx, entry))
    # sort each by step (already ordered by collection, but be safe)
    for b in branch_c_visible:
        branch_c_visible[b].sort(key=lambda t: t[1]['step'])

    def last_rect_in_lineage(branch_id: str) -> Optional[int]:
        """Return rect index of the last visible C event in branch_id or any ancestor."""
        pairs = branch_c_visible.get(branch_id)
        if pairs:
            return entry_to_rect[pairs[-1][0]]
        parent = parent_map.get(branch_id)
        if parent is not None:
            return last_rect_in_lineage(parent)
        return None

    edges: set[tuple[int, int]] = set()

    for branch_id, pairs in branch_c_visible.items():
        # Sequential edges within this branch
        for i in range(1, len(pairs)):
            a = entry_to_rect[pairs[i - 1][0]]
            b = entry_to_rect[pairs[i][0]]
            if a != b:
                edges.add((a, b))

        # Incoming edge from nearest ancestor
        parent = parent_map.get(branch_id)
        if parent is None:
            continue  # root — no predecessor
        ancestor_rect = last_rect_in_lineage(parent)
        if ancestor_rect is not None:
            first_rect = entry_to_rect[pairs[0][0]]
            if ancestor_rect != first_rect:
                edges.add((ancestor_rect, first_rect))

    # Merge edges: last visible C event in each dedup'd branch → first visible C
    # event in the surviving branch (treated as regular edges; self-loops filtered).
    for dedup_id, surviving_id in merge_map.items():
        dedup_pairs   = branch_c_visible.get(dedup_id)
        surviving_pairs = branch_c_visible.get(surviving_id)
        if not dedup_pairs or not surviving_pairs:
            continue
        last_dedup_rect    = entry_to_rect[dedup_pairs[-1][0]]
        first_surviving_rect = entry_to_rect[surviving_pairs[0][0]]
        if last_dedup_rect != first_surviving_rect:
            edges.add((last_dedup_rect, first_surviving_rect))

    return [EdgeRecord(from_idx=a, to_idx=b) for a, b in sorted(edges)]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert a cadpya JSONL simulation log to a .cadvis binary."
    )
    parser.add_argument('--log',    required=True, metavar='PATH',
                        help="Input JSONL log file")
    parser.add_argument('--map',    required=False, metavar='PATH', default=None,
                        help="Python file with map_output(s: str) -> int")
    parser.add_argument('--output', required=True, metavar='PATH',
                        help="Output .cadvis file")
    args = parser.parse_args()

    # Load output mapping function if provided
    map_fn: Optional[Callable[[str], int]] = None
    if args.map:
        map_fn = load_map_function(args.map)
        print(f"Loaded output mapping from {args.map!r}")

    # Parse log
    print(f"Reading {args.log!r} ...")
    entries = load_log(args.log)
    print(f"  {len(entries)} entries")

    # Build branch tree
    branch_entries, children_map, parent_map, merge_map = build_branch_tree(entries)
    print(f"  {len(branch_entries)} branches")

    # Discover components
    components = discover_components(entries)
    print(f"  {len(components)} components with non-null output: {components}")

    # Process each component
    writer = CadvisWriter()
    for comp_name in components:
        rects, all_events, entry_to_rect = collect_visible_events(
            comp_name, branch_entries, map_fn
        )
        edges = compute_edges(
            comp_name, branch_entries, parent_map, merge_map, all_events, entry_to_rect
        )
        comp = ComponentRecord(name=comp_name, rects=rects, edges=edges)
        writer.add_component(comp)
        print(f"  {comp_name}: {len(rects)} rects, {len(edges)} edges")

    # Write output
    output_path = args.output
    writer.write(output_path)
    size = Path(output_path).stat().st_size
    print(f"Written {output_path!r} ({size} bytes)")


if __name__ == '__main__':
    main()
