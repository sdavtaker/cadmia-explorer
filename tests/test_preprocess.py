"""Tests for preprocess/preprocess.py — branch tree, events, edges, multiplicity."""

from pathlib import Path

import pytest

from preprocess import (
    build_branch_tree,
    collect_visible_events,
    compute_edges,
    discover_components,
)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

TEST_LOGS = Path(__file__).parent.parent / "test_logs"


def make_entry(branch, parent_branch, step, component, output, time="[0.0, 1.0]", kind="atomic"):
    return {
        "branch": branch,
        "parent_branch": parent_branch,
        "step": step,
        "component": component,
        "kind": kind,
        "output": output,
        "time": time,
    }


def make_skip(branch, parent_branch, step, time="[0.0, 1.0]"):
    return make_entry(branch, parent_branch, step, "", None, time, kind="skip")


# ---------------------------------------------------------------------------
# build_branch_tree
# ---------------------------------------------------------------------------


class TestBuildBranchTree:
    def test_single_branch(self):
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0", None, 1, "A", "[2,2]"),
        ]
        branch_entries, children_map, parent_map = build_branch_tree(entries)
        assert "0" in branch_entries
        assert len(branch_entries["0"]) == 2
        assert parent_map["0"] is None

    def test_parent_child_relationship(self):
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0.0", "0", 1, "A", "[2,2]"),
            make_entry("0.1", "0", 1, "B", "[3,3]"),
        ]
        _, children_map, parent_map = build_branch_tree(entries)
        assert set(children_map["0"]) == {"0.0", "0.1"}
        assert parent_map["0.0"] == "0"
        assert parent_map["0.1"] == "0"

    def test_entries_sorted_by_step(self):
        # Deliberately out of order
        entries = [
            make_entry("0", None, 2, "A", "[3,3]"),
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0", None, 1, "A", "[2,2]"),
        ]
        branch_entries, _, _ = build_branch_tree(entries)
        steps = [e["step"] for e in branch_entries["0"]]
        assert steps == [0, 1, 2]

    def test_no_duplicate_children(self):
        # Multiple entries in the same child branch should not duplicate child registration
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0.0", "0", 1, "A", "[2,2]"),
            make_entry("0.0", "0", 2, "A", "[3,3]"),
        ]
        _, children_map, _ = build_branch_tree(entries)
        assert children_map["0"].count("0.0") == 1

    def test_skip_entries_included_in_tree(self):
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_skip("0.0", "0", 1),
        ]
        branch_entries, children_map, _ = build_branch_tree(entries)
        assert "0.0" in branch_entries
        assert "0.0" in children_map["0"]


# ---------------------------------------------------------------------------
# discover_components
# ---------------------------------------------------------------------------


class TestDiscoverComponents:
    def test_excludes_null_output_only_components(self):
        entries = [
            make_entry("0", None, 0, "A", None),   # null only
            make_entry("0", None, 1, "B", "[1,1]"),
        ]
        comps = discover_components(entries)
        assert "B" in comps
        assert "A" not in comps

    def test_excludes_skip_components(self):
        entries = [
            make_skip("0.1", "0", 0),
            make_entry("0.0", "0", 0, "A", "[1,1]"),
        ]
        comps = discover_components(entries)
        assert "" not in comps
        assert "A" in comps

    def test_component_with_mixed_null_and_non_null_included(self):
        entries = [
            make_entry("0", None, 0, "A", None),
            make_entry("0.0", "0", 1, "A", "[1,1]"),
        ]
        comps = discover_components(entries)
        assert "A" in comps

    def test_sorted_output(self):
        entries = [
            make_entry("0", None, 0, "Z", "[1,1]"),
            make_entry("0", None, 1, "A", "[2,2]"),
            make_entry("0", None, 2, "M", "[3,3]"),
        ]
        comps = discover_components(entries)
        assert comps == sorted(comps)


# ---------------------------------------------------------------------------
# collect_visible_events
# ---------------------------------------------------------------------------


class TestCollectVisibleEvents:
    def test_filters_null_outputs(self):
        entries = [
            make_entry("0", None, 0, "A", None),
            make_entry("0", None, 1, "A", "[1,1]"),
        ]
        branch_entries, _, _ = build_branch_tree(entries)
        rects, all_events, _ = collect_visible_events("A", branch_entries, None)
        assert len(rects) == 1
        assert len(all_events) == 1

    def test_multiplicity_same_time_and_output(self):
        # Two branches, same time+output → one rect with multiplicity 2
        entries = [
            make_entry("0.0", "0", 0, "A", "[1,1]", time="[0.5, 1.5]"),
            make_entry("0.1", "0", 0, "A", "[1,1]", time="[0.5, 1.5]"),
        ]
        branch_entries, _, _ = build_branch_tree(entries)
        rects, all_events, entry_to_rect = collect_visible_events("A", branch_entries, None)
        assert len(rects) == 1
        assert rects[0].multiplicity == 2

    def test_multiplicity_different_outputs_distinct(self):
        entries = [
            make_entry("0.0", "0", 0, "A", "[1,1]", time="[0.5, 1.5]"),
            make_entry("0.1", "0", 0, "A", "[2,2]", time="[0.5, 1.5]"),
        ]
        branch_entries, _, _ = build_branch_tree(entries)
        rects, _, _ = collect_visible_events("A", branch_entries, None)
        assert len(rects) == 2

    def test_output_mapping_applied(self):
        entries = [make_entry("0", None, 0, "A", "[j1:3, j1:3]")]
        branch_entries, _, _ = build_branch_tree(entries)
        rects, _, _ = collect_visible_events("A", branch_entries, lambda s: 42)
        assert rects[0].out_lo == 42.0
        assert rects[0].out_hi == 42.0
        assert rects[0].out_label == "[j1:3, j1:3]"

    def test_output_mapping_preserves_label(self):
        raw = "[j2:7, j2:7]"
        entries = [make_entry("0", None, 0, "A", raw)]
        branch_entries, _, _ = build_branch_tree(entries)
        rects, _, _ = collect_visible_events("A", branch_entries, lambda s: int(s[4]))
        assert rects[0].out_label == raw

    def test_time_interval_parsed(self):
        entries = [make_entry("0", None, 0, "A", "[1,1]", time="[0.997, 1.005]")]
        branch_entries, _, _ = build_branch_tree(entries)
        rects, _, _ = collect_visible_events("A", branch_entries, None)
        assert rects[0].time_lo == pytest.approx(0.997)
        assert rects[0].time_hi == pytest.approx(1.005)
        assert rects[0].time_lo_closed is True
        assert rects[0].time_hi_closed is True

    def test_open_interval_flags(self):
        entries = [make_entry("0", None, 0, "A", "[1,1]", time="[0.810, 0.950)")]
        branch_entries, _, _ = build_branch_tree(entries)
        rects, _, _ = collect_visible_events("A", branch_entries, None)
        assert rects[0].time_hi_closed is False


# ---------------------------------------------------------------------------
# compute_edges
# ---------------------------------------------------------------------------


class TestComputeEdges:
    def _run(self, entries, component="A", map_fn=None):
        branch_entries, _, parent_map = build_branch_tree(entries)
        rects, all_events, entry_to_rect = collect_visible_events(
            component, branch_entries, map_fn
        )
        edges = compute_edges(component, branch_entries, parent_map, all_events, entry_to_rect)
        return rects, edges

    def test_sequential_edges_within_branch(self):
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0", None, 1, "A", "[2,2]"),
            make_entry("0", None, 2, "A", "[3,3]"),
        ]
        rects, edges = self._run(entries)
        assert len(rects) == 3
        assert len(edges) == 2
        from_set = {(e.from_idx, e.to_idx) for e in edges}
        assert (0, 1) in from_set
        assert (1, 2) in from_set

    def test_fan_out_edges_to_children(self):
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0.0", "0", 1, "A", "[2,2]"),
            make_entry("0.1", "0", 1, "A", "[3,3]"),
        ]
        rects, edges = self._run(entries)
        assert len(rects) == 3
        # Rect 0 → rect in 0.0, rect 0 → rect in 0.1
        assert len(edges) == 2

    def test_no_edge_for_root_event(self):
        # If the first event in a branch has no ancestor C event → no incoming edge
        entries = [make_entry("0.0", "0", 0, "A", "[1,1]")]
        _, edges = self._run(entries)
        assert len(edges) == 0

    def test_self_loop_filtered(self):
        # Two events with same (time, output) in sibling branches → same rect → self-loop removed
        entries = [
            make_entry("0.0", "0", 0, "A", "[1,1]", time="[0.5, 1.5]"),
            make_entry("0.1", "0", 0, "A", "[1,1]", time="[0.5, 1.5]"),
        ]
        _, edges = self._run(entries)
        assert len(edges) == 0

    def test_edges_deduplicated(self):
        # Even if both paths lead to same (from, to) rect pair
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0.0", "0", 1, "A", "[2,2]"),
        ]
        _, edges = self._run(entries)
        pairs = [(e.from_idx, e.to_idx) for e in edges]
        assert len(pairs) == len(set(pairs))

    def test_traverse_null_other_component(self):
        # Branch "0" has A, then B (null for A), then child "0.0" has A
        # Edge should connect A in "0" to A in "0.0"
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0", None, 1, "B", "[9,9]"),   # different component
            make_entry("0.0", "0", 2, "A", "[2,2]"),
            make_entry("0.1", "0", 2, "A", "[3,3]"),
        ]
        rects, edges = self._run(entries)
        assert len(rects) == 3  # [1,1], [2,2], [3,3]
        assert len(edges) == 2  # [1,1]→[2,2] and [1,1]→[3,3]

    def test_traverse_ancestor_with_no_C_event(self):
        # Branch "0.0" has no A, but "0.0.0" does; "0" has the last A ancestor
        entries = [
            make_entry("0", None, 0, "A", "[1,1]"),
            make_entry("0.0", "0", 1, "B", "[9,9]"),      # no A in this branch
            make_entry("0.0.0", "0.0", 2, "A", "[2,2]"),
        ]
        rects, edges = self._run(entries)
        assert len(rects) == 2
        assert len(edges) == 1
        assert edges[0].from_idx == 0
        assert edges[0].to_idx == 1

    def test_no_sibling_edges(self):
        # Events in sibling branches should never be directly connected
        entries = [
            make_entry("0.0", "0", 0, "A", "[1,1]"),
            make_entry("0.1", "0", 0, "A", "[2,2]"),
        ]
        # No common ancestor A event → both are roots, no edges
        _, edges = self._run(entries)
        assert len(edges) == 0


# ---------------------------------------------------------------------------
# End-to-end: real test logs
# ---------------------------------------------------------------------------


class TestEndToEnd:
    def test_4gp_log(self, tmp_path):
        """4gp_log: 5 components with output, generators have 3 rects each."""
        import subprocess
        import sys

        result = subprocess.run(
            [
                sys.executable, "preprocess/preprocess.py",
                "--log", str(TEST_LOGS / "4gp_log.jsonl"),
                "--output", str(tmp_path / "4gp.cadvis"),
            ],
            capture_output=True, text=True,
        )
        assert result.returncode == 0, result.stderr
        assert (tmp_path / "4gp.cadvis").exists()
        assert "5 components" in result.stdout
        assert "G1: 3 rects" in result.stdout
        assert "P: 18 rects" in result.stdout

    def test_counter_log(self, tmp_path):
        """counter_log: only Counter has non-null output."""
        import subprocess
        import sys

        result = subprocess.run(
            [
                sys.executable, "preprocess/preprocess.py",
                "--log", str(TEST_LOGS / "counter_log.jsonl"),
                "--output", str(tmp_path / "counter.cadvis"),
            ],
            capture_output=True, text=True,
        )
        assert result.returncode == 0, result.stderr
        assert "1 components" in result.stdout
        assert "Counter" in result.stdout

    def test_job_tracker_log_requires_map(self, tmp_path):
        """job_tracker_log fails without --map because outputs are compound strings."""
        import subprocess
        import sys

        result = subprocess.run(
            [
                sys.executable, "preprocess/preprocess.py",
                "--log", str(TEST_LOGS / "job_tracker_log.jsonl"),
                "--output", str(tmp_path / "jt.cadvis"),
            ],
            capture_output=True, text=True,
        )
        assert result.returncode != 0

    def test_job_tracker_log_with_map(self, tmp_path):
        """job_tracker_log succeeds with --map and produces 4 scorer components."""
        import subprocess
        import sys

        result = subprocess.run(
            [
                sys.executable, "preprocess/preprocess.py",
                "--log", str(TEST_LOGS / "job_tracker_log.jsonl"),
                "--map", "examples/job_tracker_map.py",
                "--output", str(tmp_path / "jt.cadvis"),
            ],
            capture_output=True, text=True,
        )
        assert result.returncode == 0, result.stderr
        assert "4 components" in result.stdout
        for s in ["S1", "S2", "S3", "S4"]:
            assert s in result.stdout
