"""
interval.py — Interval string parser for cadmia-explorer.

Parses interval notation strings like:
  "[lo, hi]"   — closed on both sides
  "[lo, hi)"   — closed left, open right
  "(lo, hi]"   — open left, closed right
  "(lo, hi)"   — open on both sides

Returns an Interval dataclass with float lo/hi and bool closed flags.
"""

from __future__ import annotations
import re
from dataclasses import dataclass


_PATTERN = re.compile(
    r'^([\[\(])\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*'
    r',\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*([\]\)])$'
)


@dataclass(frozen=True)
class Interval:
    lo: float
    hi: float
    lo_closed: bool
    hi_closed: bool

    def __str__(self) -> str:
        left  = '[' if self.lo_closed else '('
        right = ']' if self.hi_closed else ')'
        return f"{left}{self.lo}, {self.hi}{right}"


def parse_interval(s: str) -> Interval:
    """
    Parse an interval string and return an Interval.

    Raises ValueError if the string does not match the expected format.
    """
    s = s.strip()
    m = _PATTERN.match(s)
    if not m:
        raise ValueError(f"Cannot parse interval: {s!r}")
    left_bracket, lo_str, hi_str, right_bracket = m.groups()
    return Interval(
        lo=float(lo_str),
        hi=float(hi_str),
        lo_closed=(left_bracket == '['),
        hi_closed=(right_bracket == ']'),
    )


if __name__ == "__main__":
    # Basic self-test
    cases = [
        ("[0.997, 1.005]",  0.997, 1.005, True,  True),
        ("[0.810, 0.950)",  0.810, 0.950, True,  False),
        ("(1.0, 2.5]",      1.0,   2.5,   False, True),
        ("(0.0, 0.0)",      0.0,   0.0,   False, False),
        ("[1, 1]",          1.0,   1.0,   True,  True),
        ("[1.997, 2.003]",  1.997, 2.003, True,  True),
        ("[-1.5e2, 3.0e1]", -150.0, 30.0, True, True),
    ]
    all_ok = True
    for raw, exp_lo, exp_hi, exp_lc, exp_hc in cases:
        iv = parse_interval(raw)
        ok = (iv.lo == exp_lo and iv.hi == exp_hi
              and iv.lo_closed == exp_lc and iv.hi_closed == exp_hc)
        status = "OK" if ok else "FAIL"
        if not ok:
            all_ok = False
        print(f"  {status}  {raw!r:30s} → {iv}")
    if all_ok:
        print("All tests passed.")
    else:
        raise SystemExit("Some tests failed.")
