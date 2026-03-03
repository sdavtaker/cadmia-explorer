"""
job_tracker_map.py — Output mapping for the job_tracker simulation.

Scorer outputs have the form "[jN:V, jN:V]" where:
  N = job number (1-based)
  V = integer score value

This mapping returns V (the score value) as the Y-axis integer.
"""

import re

_PATTERN = re.compile(r'^\[j\d+:(-?\d+),\s*j\d+:(-?\d+)\]$')


def map_output(s: str) -> int:
    """
    Map a scorer output string like "[j1:1, j1:1]" to its integer score value.
    Asserts that both sides of the interval match.
    """
    m = _PATTERN.match(s.strip())
    if not m:
        raise ValueError(f"Unrecognized job_tracker output format: {s!r}")
    lo, hi = int(m.group(1)), int(m.group(2))
    if lo != hi:
        raise ValueError(f"Expected punctual interval, got lo={lo} hi={hi} in: {s!r}")
    return lo
