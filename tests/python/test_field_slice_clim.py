"""2D slice color scale of scripts/field_slice_export.py."""

import os
import sys

import numpy as np
import pytest

pytest.importorskip("pyvista")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import field_slice_export as fse  # noqa: E402


def test_log_scale_limited_to_40db_below_slice_max():
    img = np.array([[1e-6, 0.5], [2.0, np.nan]])
    lo, hi = fse._slice_clim(img, None, "|E|", True)
    assert hi == pytest.approx(np.log10(2.0))
    assert hi - lo == pytest.approx(2.0)  # |E| is an amplitude: 40 dB = 2 decades
    lo, hi = fse._slice_clim(img, None, "U_e", True)  # energy density: 10*log10
    assert hi - lo == pytest.approx(4.0)


def test_linear_scale_uses_slice_percentiles():
    img = np.linspace(0.0, 100.0, 101).reshape(1, -1)
    lo, hi = fse._slice_clim(img, None, "|E|", False)
    assert lo == pytest.approx(2.0)
    assert hi == pytest.approx(98.0)


def test_log_scale_stops_at_slice_min_within_40db():
    img = np.array([[0.1, 0.5], [2.0, np.nan]])  # 26 dB span for |E|
    lo, hi = fse._slice_clim(img, None, "|E|", True)
    assert lo == pytest.approx(-1.0)
    assert hi == pytest.approx(np.log10(2.0))
