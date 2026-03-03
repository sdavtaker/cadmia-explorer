"""Tests for preprocess/interval.py"""

import pytest
from interval import Interval, parse_interval


class TestParseInterval:
    def test_closed_both(self):
        iv = parse_interval("[1.0, 2.0]")
        assert iv == Interval(lo=1.0, hi=2.0, lo_closed=True, hi_closed=True)

    def test_open_both(self):
        iv = parse_interval("(0.0, 1.0)")
        assert iv == Interval(lo=0.0, hi=1.0, lo_closed=False, hi_closed=False)

    def test_open_right(self):
        iv = parse_interval("[0.810, 0.950)")
        assert iv.lo_closed is True
        assert iv.hi_closed is False
        assert iv.lo == pytest.approx(0.810)
        assert iv.hi == pytest.approx(0.950)

    def test_open_left(self):
        iv = parse_interval("(1.0, 2.5]")
        assert iv.lo_closed is False
        assert iv.hi_closed is True

    def test_integer_values(self):
        iv = parse_interval("[1, 1]")
        assert iv.lo == 1.0
        assert iv.hi == 1.0

    def test_negative_values(self):
        iv = parse_interval("[-1.5, 3.0]")
        assert iv.lo == pytest.approx(-1.5)
        assert iv.hi == pytest.approx(3.0)

    def test_scientific_notation(self):
        iv = parse_interval("[-1.5e2, 3.0e1]")
        assert iv.lo == pytest.approx(-150.0)
        assert iv.hi == pytest.approx(30.0)

    def test_whitespace_tolerance(self):
        # Extra spaces around values and brackets
        iv = parse_interval("[  0.997 ,  1.005  ]")
        assert iv.lo == pytest.approx(0.997)
        assert iv.hi == pytest.approx(1.005)

    def test_simulation_time_example(self):
        iv = parse_interval("[0.997, 1.005]")
        assert iv.lo == pytest.approx(0.997)
        assert iv.hi == pytest.approx(1.005)
        assert iv.lo_closed is True
        assert iv.hi_closed is True

    def test_str_roundtrip_closed(self):
        iv = parse_interval("[1.0, 2.0]")
        assert str(iv) == "[1.0, 2.0]"

    def test_str_roundtrip_open(self):
        iv = parse_interval("(0.0, 1.0)")
        assert str(iv) == "(0.0, 1.0)"

    def test_invalid_compound_string(self):
        with pytest.raises(ValueError, match="Cannot parse interval"):
            parse_interval("[j1:1, j1:1]")

    def test_invalid_plain_string(self):
        with pytest.raises(ValueError, match="Cannot parse interval"):
            parse_interval("not an interval")

    def test_invalid_empty(self):
        with pytest.raises(ValueError):
            parse_interval("")

    def test_invalid_missing_comma(self):
        with pytest.raises(ValueError):
            parse_interval("[1.0 2.0]")
