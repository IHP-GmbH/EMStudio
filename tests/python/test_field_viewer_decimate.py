"""Arrow decimation in scripts/field_viewer.py (needs PySide6 + pyvistaqt)."""

import os
import sys

import numpy as np
import pytest

pytest.importorskip("pyvistaqt")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import field_viewer  # noqa: E402


def test_one_point_per_cell_keeps_largest():
    pts = np.array([[0.0, 0, 0], [0.1, 0, 0], [1.5, 0, 0], [1.6, 0, 0]])
    w = np.array([1.0, 5.0, 2.0, 1.0])
    assert list(field_viewer._decimate_indices(pts, 1.0, w)) == [1, 2]


def test_zero_spacing_keeps_all():
    pts = np.random.default_rng(0).random((50, 3))
    assert len(field_viewer._decimate_indices(pts, 0.0, np.ones(50))) == 50


def test_large_cloud_is_fast():
    pts = np.random.default_rng(1).random((500_000, 3))
    idx = field_viewer._decimate_indices(pts, 0.01, np.ones(len(pts)))
    assert 0 < len(idx) <= 101 ** 3
