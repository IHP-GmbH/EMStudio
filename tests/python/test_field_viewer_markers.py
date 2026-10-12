"""Value markers and jump-to-maximum of scripts/field_viewer.py (needs PySide6 + pyvistaqt).

Run with QT_QPA_PLATFORM=offscreen. The model is a 4 x 4 x 4 cube with a linear
temperature (interpolation is exact) and two Elmer bodies split at x = 2.
"""

import os
import sys
import time

import numpy as np
import pytest

pv = pytest.importorskip("pyvista")
pytest.importorskip("pyvistaqt")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import field_viewer  # noqa: E402
from PySide6.QtCore import QEvent, QPointF, Qt  # noqa: E402
from PySide6.QtGui import QMouseEvent  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402


def _temperature(p):
    p = np.asarray(p, dtype=float)
    return 300.0 + p[..., 0] + 2.0 * p[..., 1] + 3.0 * p[..., 2]


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


@pytest.fixture(scope="module")
def window(app, tmp_path_factory):
    # One window for the module: repeated QtInteractor windows abort VTK offscreen.
    tmp_path = tmp_path_factory.mktemp("markers")
    grid = pv.ImageData(dimensions=(5, 5, 5)).cast_to_unstructured_grid()
    grid.point_data["temperature"] = _temperature(grid.points)
    grid.point_data["heat"] = 2.0 * _temperature(grid.points)
    grid.cell_data["GeometryIds"] = np.where(grid.cell_centers().points[:, 0] < 2.0, 1, 2) \
        .astype(np.int32)
    path = str(tmp_path / "thermal_results_t0001.vtu")
    grid.save(path)
    win = field_viewer.FieldViewerWindow([path], "elmer_thermal", nav_style="emstudio")
    win.resize(900, 700)
    win.show()
    app.processEvents()
    win._select_array("temperature")
    yield win
    win.close()


@pytest.fixture(autouse=True)
def clean(window, app):
    window._clear_markers()
    window.probe_cb.setChecked(False)
    window.temp_unit_combo.setCurrentIndex(0)
    window.clip_enabled_cb.setChecked(False)
    window.slice_only_cb.setChecked(False)
    window._select_array("temperature")
    _settle(window, app)
    yield


def _settle(win, app, timeout=10.0):
    end = time.time() + timeout
    while not win.clip_settled() and time.time() < end:
        app.processEvents()
        time.sleep(0.01)
    app.processEvents()


def test_value_inside_is_interpolated(window):
    values = window._values_at([[1.5, 2.5, 0.5], [0.25, 0.75, 3.5]])
    assert values == pytest.approx(_temperature([[1.5, 2.5, 0.5], [0.25, 0.75, 3.5]]))


def test_value_just_outside_takes_nearest_node(window):
    value = window._values_at([[4.0 + 1e-3, 4.0, 4.0 + 1e-3]])[0]
    assert value == pytest.approx(_temperature([4.0, 4.0, 4.0]))


def test_cut_plane_max_lies_on_plane(window, app):
    window.clip_enabled_cb.setChecked(True)  # Z, slider in the middle: z = 2
    _settle(window, app)
    marker = window._jump_to_cut_plane_max()
    assert marker["pos"] == pytest.approx([4.0, 4.0, 2.0])
    assert marker["value"] == pytest.approx(_temperature([4.0, 4.0, 2.0]))
    window.slice_only_cb.setChecked(True)  # uses the cached 2D section
    _settle(window, app)
    assert window._jump_to_cut_plane_max()["pos"] == pytest.approx([4.0, 4.0, 2.0])


def test_cut_plane_max_needs_clip(window):
    assert window._jump_to_cut_plane_max() is None


def test_surface_max_stays_on_clicked_body(window):
    mesh = window._full_mesh
    ids = window._geometry_ids(mesh)
    cold_cell = int(np.flatnonzero(ids == 1)[0])
    assert window._body_id(cold_cell, mesh) == 1
    marker = window._jump_to_surface_max(cold_cell, mesh)
    # Hottest point of body 1 (x <= 2), not the global maximum at x = 4.
    assert marker["pos"] == pytest.approx([2.0, 4.0, 4.0])


def test_surface_max_without_body_ids_uses_whole_surface(window):
    mesh = window._full_mesh.copy()
    for key in list(mesh.cell_data.keys()):
        del mesh.cell_data[key]
    assert window._body_id(0, mesh) is None
    marker = window._jump_to_surface_max(0, mesh)
    assert marker["pos"] == pytest.approx([4.0, 4.0, 4.0])


def test_field_change_rereads_markers_and_drops_outside(window, app):
    inside = window._add_marker([1.0, 1.0, 1.0])
    outside = window._add_marker([1.0, 1.0, 1.0])
    outside["pos"] = np.array([10.0, 1.0, 1.0])
    window._select_array("heat")
    app.processEvents()
    assert [m["id"] for m in window._markers] == [inside["id"]]
    assert inside["value"] == pytest.approx(2.0 * _temperature([1.0, 1.0, 1.0]))
    assert window.marker_table.rowCount() == 1


def test_value_text(window):
    assert window._format_marker_value(373.15) == "373.15 K"
    window._select_array("heat")
    assert window._format_marker_value(1234.5678) == "1235"
    assert window._format_marker_value(float("nan")) == "-"


def test_marker_list_and_clear(window):
    window._add_marker([1.0, 2.0, 3.0])
    window._add_marker([2.0, 2.0, 2.0])
    assert window.marker_table.rowCount() == 2
    assert not window.marker_panel.isHidden()
    window._remove_markers([1])
    assert [m["id"] for m in window._markers] == [2]
    window._clear_markers()
    assert window.marker_table.rowCount() == 0
    assert window.marker_panel.isHidden()
    assert window._add_marker([1.0, 1.0, 1.0])["id"] == 1  # numbering restarts


def _click(win, app, start, end):
    plot = win.plotter
    for etype, pos in ((QEvent.MouseButtonPress, start), (QEvent.MouseButtonRelease, end)):
        buttons = Qt.LeftButton if etype == QEvent.MouseButtonPress else Qt.NoButton
        ev = QMouseEvent(etype, QPointF(pos), QPointF(plot.mapToGlobal(pos)),
                         Qt.LeftButton, buttons, Qt.NoModifier)
        QApplication.sendEvent(plot, ev)
    for _ in range(5):
        app.processEvents()


def test_probe_click_places_marker_on_top_face(window, app):
    window._set_view("Z", 1)
    app.processEvents()
    window.plotter.render()
    center = window.plotter.rect().center()
    hit = window._pick_at(center)
    assert hit is not None
    assert hit[0][2] == pytest.approx(4.0, abs=1e-3)

    window.probe_cb.setChecked(True)
    _click(window, app, center, center)
    assert len(window._markers) == 1
    pos = window._markers[0]["pos"]
    assert pos[2] == pytest.approx(4.0, abs=1e-3)
    assert pos[:2] == pytest.approx(hit[0][:2], abs=1e-3)
    # The marker list opened beside the plot, so the view center moved with it.
    center = window.plotter.rect().center()
    assert window._marker_near(center)["id"] == 1

    # A drag orbits, it doesn't place a marker; nor does a click with Probe off.
    _click(window, app, center, center + type(center)(30, 0))
    window.probe_cb.setChecked(False)
    _click(window, app, center, center)
    assert len(window._markers) == 1


def test_id_then_value_columns_and_temperature_units(window, app):
    window._add_marker([1.0, 1.0, 1.0])  # 306 K; model minimum 300 K at the origin
    table = window.marker_table
    assert table.horizontalHeaderItem(1).text() == "Value [K]"
    assert [table.item(0, c).text() for c in range(2)] == ["1", "306.00"]
    assert not window.temp_unit_combo.isHidden()

    window.temp_unit_combo.setCurrentIndex(window.temp_unit_combo.findData("C"))
    assert table.horizontalHeaderItem(1).text() == "Value [°C]"
    assert table.item(0, 1).text() == "32.85"
    assert window._marker_label(window._markers[0]) == "1: 32.85 °C"

    window.temp_unit_combo.setCurrentIndex(window.temp_unit_combo.findData("dT"))
    assert table.horizontalHeaderItem(1).text() == "ΔT [K]"
    assert table.item(0, 1).text() == "6.00"
    assert window._marker_label(window._markers[0]) == "1: ΔT 6.00 K"
    window._copy_markers()
    lines = QApplication.clipboard().text().splitlines()
    assert lines[0].startswith("#\tΔT [K]\t")
    assert lines[1].split("\t")[:2] == ["1", "6"]

    # Not a temperature: no unit choice, plain value.
    window._select_array("heat")
    app.processEvents()
    assert window.temp_unit_combo.isHidden()
    assert table.horizontalHeaderItem(1).text() == "Value"
    assert table.item(0, 1).text() == "612"
