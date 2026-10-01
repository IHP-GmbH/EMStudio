"""Thermal 2D slice keeps GDS X/Y (no axis swap) — scripts/field_slice_export.py."""

import json
import os
import struct
import sys

import numpy as np
import pytest

pv = pytest.importorskip("pyvista")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import field_slice_export as fse  # noqa: E402


def _thermal_dump(path):
    # Elmer thermal: meters, off-centre and non-square in XY (like a real heat spreader run).
    grid = pv.ImageData(dimensions=(41, 21, 6), spacing=(50e-6, 50e-6, 20e-6),
                        origin=(-2.0e-3, 0.1e-3, 0.0)).cast_to_unstructured_grid()
    hot = np.array([-1.5e-3, 0.8e-3, 0.05e-3])  # hot spot, in meters
    d2 = np.sum((grid.points - hot) ** 2, axis=1)
    grid.point_data["temperature"] = 300.0 + 25.0 * np.exp(-d2 / (2 * (80e-6) ** 2))
    grid.save(path)
    return grid


def test_thermal_slice_bounds_and_hot_spot_stay_on_their_axes(tmp_path):
    vtu = str(tmp_path / "thermal_results_t0001.vtu")
    mesh = _thermal_dump(vtu)
    out = tmp_path / "out"
    out.mkdir()
    fse.export_slice(mesh_path=vtu, outdir=str(out), z_um=50.0, resolution=128,
                     log_scale=False, quantity=None, scale_to_um=None,
                     source="elmer_thermal")
    meta = json.loads((out / "field_slice_meta.json").read_text())
    assert meta["version"] >= 2  # EMStudio ignores older (axis-swapped) caches

    x0, x1, y0, y1 = (b * 1e6 for b in mesh.bounds[:4])
    assert meta["xmin_um"] == pytest.approx(x0) and meta["xmax_um"] == pytest.approx(x1)
    assert meta["ymin_um"] == pytest.approx(y0) and meta["ymax_um"] == pytest.approx(y1)

    raw = (out / "field_slice_values.bin").read_bytes()
    assert raw[:4] == b"EMFV"
    _, ny, nx = struct.unpack("<III", raw[4:16])
    vals = np.frombuffer(raw[16:], dtype=np.float32).reshape(ny, nx)  # row 0 = ymax
    r, c = np.unravel_index(np.nanargmax(vals), vals.shape)
    x = meta["xmin_um"] + c / (nx - 1) * (meta["xmax_um"] - meta["xmin_um"])
    y = meta["ymax_um"] - r / (ny - 1) * (meta["ymax_um"] - meta["ymin_um"])
    assert x == pytest.approx(-1500.0, abs=60.0)
    assert y == pytest.approx(800.0, abs=60.0)
