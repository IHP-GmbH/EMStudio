"""Mouse presets and keys of scripts/field_viewer.py (needs PySide6 + pyvistaqt).

Run with QT_QPA_PLATFORM=offscreen; the window is created but never rendered.
"""

import os
import sys

import numpy as np
import pytest

pv = pytest.importorskip("pyvista")
pytest.importorskip("pyvistaqt")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import field_viewer  # noqa: E402
from PySide6.QtCore import QEvent, QPoint, QPointF, Qt  # noqa: E402
from PySide6.QtGui import QKeyEvent, QWheelEvent  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


@pytest.fixture(scope="module")
def window(app, tmp_path_factory):
    # One window for the module: repeated QtInteractor windows abort VTK offscreen.
    tmp_path = tmp_path_factory.mktemp("nav")
    grid = pv.ImageData(dimensions=(5, 5, 5)).cast_to_unstructured_grid()
    rng = np.random.default_rng(0)
    grid.point_data["E_real"] = rng.random((grid.n_points, 3))
    grid.point_data["E_imag"] = rng.random((grid.n_points, 3))
    path = str(tmp_path / "field.vtu")
    grid.save(path)
    win = field_viewer.FieldViewerWindow([path], "palace", nav_style="emstudio")
    win.show()
    app.processEvents()
    yield win
    try:
        win.close()
    except RuntimeError:  # already deleted (replaced_by closes it)
        pass


def _camera_state(win):
    cam = win.plotter.camera
    pos = np.array(cam.position)
    return pos, float(np.linalg.norm(pos - np.array(cam.focal_point)))


def _wheel(win, dy, mods=Qt.NoModifier):
    ev = QWheelEvent(QPointF(50, 50), QPointF(50, 50), QPoint(0, 0), QPoint(0, dy),
                     Qt.NoButton, mods, Qt.NoScrollPhase, False)
    QApplication.sendEvent(win.plotter, ev)


def test_press_mapping_per_style(window):
    window.set_nav_style("emstudio")
    assert window._mapped_press(Qt.RightButton, Qt.NoModifier) == (Qt.LeftButton, Qt.NoModifier)
    assert window._mapped_press(Qt.LeftButton, Qt.ControlModifier) == (Qt.LeftButton, Qt.NoModifier)
    assert window._mapped_press(Qt.LeftButton, Qt.ShiftModifier) == (Qt.MiddleButton, Qt.NoModifier)
    window.set_nav_style("setupem")
    # VTK trackball as is: right = dolly, Ctrl+left = roll
    assert window._mapped_press(Qt.RightButton, Qt.NoModifier) == (Qt.RightButton, Qt.NoModifier)
    assert window._mapped_press(Qt.LeftButton, Qt.ControlModifier) == (Qt.LeftButton, Qt.ControlModifier)
    assert window._mapped_press(Qt.LeftButton, Qt.AltModifier) == (Qt.MiddleButton, Qt.NoModifier)
    window.set_nav_style("bogus")
    assert window.nav_style == "setupem"


def test_emstudio_wheel_orbits_without_zoom(window):
    window.set_nav_style("emstudio")
    pos0, dist0 = _camera_state(window)
    _wheel(window, 120)
    pos1, dist1 = _camera_state(window)
    assert np.linalg.norm(pos1 - pos0) > 1e-9
    assert dist1 == pytest.approx(dist0, rel=1e-6)


def test_setupem_wheel_zooms(window):
    window.set_nav_style("setupem")
    window.orthographic_cb.setChecked(False)
    _, dist0 = _camera_state(window)
    _wheel(window, 120)
    _, dist1 = _camera_state(window)
    assert dist1 < dist0


def test_keys_drive_view_and_vtk_keys_are_swallowed(window):
    # X: camera on +X, parallel projection on
    QApplication.sendEvent(window.plotter, QKeyEvent(QEvent.KeyPress, Qt.Key_X, Qt.NoModifier, "x"))
    pos, _ = _camera_state(window)
    focal = np.array(window.plotter.camera.focal_point)
    assert (pos - focal)[0] > 0 and abs((pos - focal)[1]) < 1e-9
    assert window.orthographic_cb.isChecked()
    # O toggles projection
    QApplication.sendEvent(window.plotter, QKeyEvent(QEvent.KeyPress, Qt.Key_O, Qt.NoModifier, "o"))
    assert not window.orthographic_cb.isChecked()
    # VTK's 'w' (wireframe) must not reach VTK: the filter consumes it
    ev = QKeyEvent(QEvent.KeyPress, Qt.Key_W, Qt.NoModifier, "w")
    assert window.eventFilter(window.plotter, ev) is True
    # PgUp enables the clip and moves the plane
    before = window.clip_slider.value()
    QApplication.sendEvent(window.plotter, QKeyEvent(QEvent.KeyPress, Qt.Key_PageUp, Qt.NoModifier))
    assert window.clip_enabled_cb.isChecked()
    assert window.clip_slider.value() > before


def test_replaced_window_keeps_style(window, monkeypatch):
    # A second real QtInteractor aborts VTK on the offscreen platform; capture the call instead.
    created = {}

    class _Stub:
        def __init__(self, files, source, **kwargs):
            created.update(kwargs)
            self.plotter = self

        def setGeometry(self, _rect):
            pass

        def show(self):
            pass

        def setFocus(self):
            pass

    monkeypatch.setattr(field_viewer, "FieldViewerWindow", _Stub)
    monkeypatch.setattr(window, "close", lambda: True)  # keep the shared window
    window.set_nav_style("emstudio")
    window.replaced_by(window.file_paths, window.source)
    assert created["nav_style"] == "emstudio"


def test_log_range_dropdown_sets_min_relative_to_max(window):
    window.clim_max_edit.setText("100")
    window.log_scale_cb.setChecked(False)
    assert [window.log_range_combo.itemData(i) for i in range(window.log_range_combo.count())] \
        == [10, 20, 30, 40, 50, 60, 70]
    assert window.log_range_combo.currentData() == 70  # default
    window.log_scale_cb.setChecked(True)
    assert window.clim_min_row.isHidden() and not window.log_range_row.isHidden()
    amplitude = field_viewer.field_io.is_amplitude_array(window._current_array())
    per_decade = 20.0 if amplitude else 10.0
    lo, hi = window._get_clim()
    assert hi == 100.0 and lo == pytest.approx(100.0 / 10 ** (70 / per_decade))
    window.set_log_range_db(60)
    assert window._get_clim()[0] == pytest.approx(100.0 / 10 ** (60 / per_decade))
    window.set_log_range_db(25)  # CLI value outside the list gets its own entry
    assert window.log_range_combo.currentData() == 25
    window.log_scale_cb.setChecked(False)
    assert not window.clim_min_row.isHidden() and window.log_range_row.isHidden()
    window.set_log_range_db(70)  # the window is shared with other tests


def test_startup_log_default_shows_dropdown(window):
    # EM sources start with Log on (signals blocked): the dropdown must replace the Min input.
    window._load_mesh()
    assert window.log_scale_cb.isChecked()
    assert window.clim_min_row.isHidden() and not window.log_range_row.isHidden()
    assert window.log_range_combo.currentData() == 70


def test_opacity_redraws_on_release_or_after_settling(window, app):
    from PySide6.QtTest import QTest
    window._schedule_redraw = lambda *a: calls.append(1)
    calls = []
    slider = window.opacity_slider
    slider.setSliderDown(True)
    slider.setValue(60)
    slider.setValue(50)
    assert calls == []  # nothing while the handle is held
    slider.setSliderDown(False)  # emits sliderReleased
    assert calls == [1]
    # wheel / key steps: one redraw after the value settles, not one per step
    calls.clear()
    slider.setValue(40)
    slider.setValue(30)
    assert calls == []
    QTest.qWait(450)
    assert calls == [1]
    slider.setValue(100)
    QTest.qWait(450)
    del window._schedule_redraw


def test_file_switch_keeps_field_pane_when_field_exists(window, tmp_path):
    first = window.file_path
    grid = pv.read(first)
    for name in ("E_real", "E_imag"):
        grid.point_data[name] = grid.point_data[name] * 5.0
    same_fields = str(tmp_path / "same_fields.vtu")
    grid.save(same_fields)
    other = pv.ImageData(dimensions=(5, 5, 5), spacing=(2, 2, 2)).cast_to_unstructured_grid()
    other.point_data["B_real"] = np.ones((other.n_points, 3))
    other.point_data["B_imag"] = np.ones((other.n_points, 3))
    other_fields = str(tmp_path / "other_fields.vtu")
    other.save(other_fields)

    # A non-default field and manual settings.
    names = [window.array_combo.itemData(i) for i in range(window.array_combo.count())]
    names = [n for n in names if isinstance(n, str) and n]
    chosen = next(n for n in names if n != window._current_array())
    window._select_array(chosen)
    window.log_scale_cb.setChecked(False)
    window.clim_min_edit.setText("0.5")
    window.clim_max_edit.setText("2")

    window._camera_needs_reset = False
    window.plotter.camera.position = (7.0, -5.0, 9.0)
    window.plotter.camera.zoom(1.7)
    camera = (window.plotter.camera.position, window.plotter.camera.parallel_scale,
              window.plotter.camera.view_angle)
    window.clip_slider.setValue(window.clip_slider.maximum() // 4)
    clip = window.clip_slider.value()

    window._switch_to_file(same_fields)
    # Same model extent: view, zoom and clip plane stay.
    assert not window._camera_needs_reset
    assert (window.plotter.camera.position, window.plotter.camera.parallel_scale,
            window.plotter.camera.view_angle) == camera
    assert window.clip_slider.value() == clip
    assert window._current_array() == chosen
    assert not window.log_scale_cb.isChecked()
    assert (window.clim_min_edit.text(), window.clim_max_edit.text()) == ("0.5", "2")

    # The field doesn't exist there: the file's defaults (field, Log, data range).
    window.log_scale_cb.setChecked(True)
    window._switch_to_file(other_fields)
    # Other extent: camera and clip plane are reset.
    assert window._camera_needs_reset
    assert window.clip_slider.value() == window.clip_slider.maximum() // 2
    default_array, _cmap, default_log = field_viewer.field_io.pick_default(
        window._full_mesh, window.source)
    assert window._current_array() == default_array != chosen
    assert window.log_scale_cb.isChecked() == default_log
    assert window.clim_max_edit.text() not in ("", "2")

    window._switch_to_file(first)  # the window is shared with other tests
