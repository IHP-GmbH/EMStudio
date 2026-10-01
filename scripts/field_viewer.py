#!/usr/bin/env python3
# -*- coding: utf-8 -*-
########################################################################
#
# Copyright 2025-2026 Volker Muehlhaus and IHP PDK Authors
#
# Licensed under the GNU General Public License, Version 3.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.gnu.org/licenses/gpl-3.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
########################################################################
"""EMStudio Field 3D viewer (Layout Field -> 3D), a separate Qt window.

Port of setupEM's field_viewer.py (the reference implementation of
FIELD_VIEWER_SPEC.md). File discovery, reading, units and the field list
come from field_io.py, shared with the Layout 2D slice exporter.

Launch:
    python field_viewer.py <file.pvd|.pvtu|.vtu|_abs.vtr> [--source ...]
    python field_viewer.py --run-path <..._data> --source palace|elmer_em|elmer_thermal|openems
    ... --screenshot out.png   (headless, same code path)

EMStudio passes --stdin-control: while the window is open, a JSON line on
stdin ({"files": [...], "run_path": ..., "source": ..., "cycle": N}) makes
the window reload those files (or just come to the front if unchanged).
{"nav_style": "emstudio"|"setupem"} alone switches the mouse navigation
without raising the window.

Navigation (--nav-style, EMStudio Setup -> Key Bindings):
    setupem   VTK trackball: left drag orbit, Ctrl+left roll, right drag and
              wheel zoom, middle / Shift+left / Alt+left pan.
    emstudio  As the EMStudio Layout 3D view: left or right drag and wheel /
              trackpad scroll orbit, Ctrl+wheel zoom, middle / Shift+left /
              Alt+left pan.
Keys (plot focused): R/F/Home reset camera, I isometric, X/Y/Z axis views
(Shift: negative side), +/- zoom, arrows pan, O parallel projection,
M find max., A arrows, PgUp/PgDn clip plane, Ctrl+C copy. VTK's own
letter keys (w, s, 3, q, e, ...) are disabled.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time

try:
    import numpy as np
    import pyvista as pv
    from PySide6.QtWidgets import (
        QApplication, QDialog, QVBoxLayout, QHBoxLayout, QGridLayout, QGroupBox,
        QLabel, QPushButton, QRadioButton, QButtonGroup, QCheckBox, QWidget,
        QSlider, QComboBox, QLineEdit, QStyleFactory, QColorDialog, QMenu,
    )
    from PySide6.QtCore import Qt, QObject, QThread, QTimer, Signal, QEvent, QPoint
    from PySide6.QtGui import QShortcut, QKeySequence, QColor, QIcon, QMouseEvent
    from pyvistaqt import QtInteractor
except ImportError as _exc:  # pragma: no cover - depends on the host Python
    print(f"field_viewer: missing Python package ({_exc}).\n"
          f"  Install into {sys.executable}:\n"
          f"    python -m pip install pyvista pyvistaqt PySide6", file=sys.stderr, flush=True)
    sys.exit(2)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import field_io  # noqa: E402

_AXIS_NORMAL = {"X": (1.0, 0.0, 0.0), "Y": (0.0, 1.0, 0.0), "Z": (0.0, 0.0, 1.0)}
_AXIS_BOUNDS_INDEX = {"X": (0, 1), "Y": (2, 3), "Z": (4, 5)}
_AXIS_POINT_INDEX = {"X": 0, "Y": 1, "Z": 2}
# Up vector for the axis-view buttons: +Z for X/Y views, +Y for Z views.
_VIEW_UP = {"X": (0.0, 0.0, 1.0), "Y": (0.0, 0.0, 1.0), "Z": (0.0, 1.0, 0.0)}
# Mouse navigation presets (same ids as EMStudio's VIEWER_NAV_STYLE).
NAV_STYLES = ("emstudio", "setupem")
# Log color scale: range below the maximum, in dB (dropdown; default 70).
_LOG_RANGE_DB_CHOICES = (10, 20, 30, 40, 50, 60, 70)
_LOG_RANGE_DB_DEFAULT = 70
# Opacity changes that are not a drag (wheel, keys) redraw after this pause.
_OPACITY_SETTLE_MS = 300
# Right press that moves less than this is a click (context menu), not a drag.
_CLICK_SLOP_PX = 6

# Fine enough that "Find max." lands within a nanometre on mm-scale domains.
_SLIDER_STEPS = 1_000_000
_SCREENSHOT_WINDOW_SIZE = (1280, 960)

_ARROW_SIZE_DEFAULT_PERCENT = 8
_ARROW_SIZE_STEP_PERCENT = 0.2
_ARROW_SIZE_MIN_PERCENT = 0.2
_ARROW_SIZE_MAX_PERCENT = 10
# Shortest arrow is this fraction of the longest (log-magnitude mapping).
_ARROW_MIN_LENGTH_RATIO = 0.15
# Glyph decimation tolerance as a multiple of the arrow-size fraction.
_ARROW_DECIMATION_RATIO = 0.25

_GLYPH_SCALE_KEY = "_glyph_arrow_length"


def _find_icon(explicit: str | None) -> str | None:
    if explicit and os.path.isfile(explicit):
        return os.path.abspath(explicit)
    here = os.path.dirname(os.path.abspath(__file__))
    for rel in ("../appicon.ico", "appicon.ico", "../icons/logo.png", "icons/logo.png"):
        p = os.path.normpath(os.path.join(here, rel))
        if os.path.isfile(p):
            return p
    return None


def _colormap_for(name, rng, use_log):
    """cmap argument for add_mesh. The EMStudio scale is a LookupTable whose
    range / log scale must be set here (add_mesh ignores clim and log_scale
    for a LookupTable). Returns (cmap, clim, log_scale) to pass on."""
    if name != field_io.EMSTUDIO_CMAP:
        return name, rng, use_log
    rgb = field_io.emstudio_colormap_rgb(np.linspace(0.0, 1.0, 256))
    lut = pv.LookupTable(values=(np.column_stack([rgb, np.ones(256)]) * 255).astype(np.uint8))
    lo, hi = rng
    if not hi > lo:
        hi = lo + (abs(lo) if lo else 1.0)
    lut.scalar_range = (lo, hi)
    lut.log_scale = bool(use_log)
    return lut, None, False


def _exact_clip_by_axis(mesh, axis, position, sign, slice_only=False):
    """Exact geometric cut (cells straddling the plane are cut, giving a flat
    face), or the bare 2D section for slice_only. Slow on large meshes, so
    the window always runs it through _ClipWorker."""
    base_normal = _AXIS_NORMAL[axis]
    origin = tuple(position if n == 1.0 else 0.0 for n in base_normal)
    if slice_only:
        return mesh.slice(normal=base_normal, origin=origin)
    kept = mesh.clip(normal=tuple(n * sign for n in base_normal), origin=origin)
    if kept.n_cells == 0:
        # Plane on the model's outer face (e.g. "Find max." at a boundary
        # hotspot): keep the other side rather than show nothing.
        kept = mesh.clip(normal=tuple(-n * sign for n in base_normal), origin=origin)
    return kept


def _same_extent(a, b, tolerance=0.01):
    """True if two (xmin, xmax, ymin, ymax, zmin, zmax) bounds match within
    tolerance x the larger diagonal (same model, e.g. another frequency)."""
    diag = max(np.linalg.norm(np.subtract(a[1::2], a[0::2])),
               np.linalg.norm(np.subtract(b[1::2], b[0::2])))
    return bool(np.max(np.abs(np.subtract(a, b))) <= tolerance * diag) if diag > 0 else a == b


def _decimate_indices(points, spacing, weights):
    """One point per cube of size ``spacing`` (the largest-weight one, so
    hotspots survive). Replaces VTK's glyph(tolerance=...), whose point
    merging becomes extremely slow for small tolerances on large meshes."""
    n = len(points)
    if n == 0 or spacing <= 0:
        return np.arange(n)
    keys = np.floor((points - points.min(axis=0)) / spacing).astype(np.int64)
    dims = keys.max(axis=0) + 1
    if float(np.prod(dims.astype(float))) >= 2.0 ** 62:
        return np.arange(n)
    flat = np.ravel_multi_index(keys.T, dims)
    order = np.argsort(-np.asarray(weights), kind="stable")
    _, first = np.unique(flat[order], return_index=True)
    return np.sort(order[first])


class _ClipWorker(QThread):
    """Background clip on a private copy of the mesh; the result is tagged
    with the key it was computed for (axis, position, sign, slice_only,
    mesh generation)."""
    succeeded = Signal(object, str, float, int, bool, int)
    failed = Signal(str, str, float, int, bool, int)

    def __init__(self, mesh, axis, position, sign, slice_only, generation):
        super().__init__()
        self._mesh = mesh
        self._key = (axis, position, sign, slice_only, generation)

    def run(self):
        try:
            result = _exact_clip_by_axis(self._mesh, *self._key[:4])
        except Exception as exc:
            self.failed.emit(str(exc), *self._key)
            return
        self.succeeded.emit(result, *self._key)


class _StdinListener(QObject):
    """Reads JSON control lines from stdin on a daemon thread (EMStudio)."""
    message = Signal(dict)

    def start(self):
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        for line in sys.stdin:
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                self.message.emit(json.loads(line))
            except ValueError:
                continue


class FieldViewerWindow(QDialog):
    """Top-level field viewer window for a list of candidate result files."""

    def __init__(self, file_paths, source, off_screen=False, icon_path=None, nav_style="setupem"):
        super().__init__()
        self.setAttribute(Qt.WA_DeleteOnClose)
        self.nav_style = nav_style if nav_style in NAV_STYLES else "setupem"
        self._right_press_pos = None
        self._icon_path = icon_path
        if icon_path:
            self.setWindowIcon(QIcon(icon_path))
        self.file_paths = list(file_paths)
        # AMR iteration copies are hidden unless requested; if every file is
        # one, list them all rather than nothing.
        self._final_paths = [p for p in self.file_paths
                             if not field_io.is_amr_iteration_path(p)] or self.file_paths
        self._visible_paths = self._final_paths
        self.file_path = self._visible_paths[0]
        self.source = source
        self._file_labels = field_io.build_file_labels(self.file_paths, source)
        self._off_screen = off_screen

        self._full_mesh = None
        self._mesh_actor = None
        self._vector_actor = None
        self._current_axis = "Z"
        self._load_error = None
        self._scale_um = 1.0
        self._entries = {}
        self._current_cmap = field_io.EMSTUDIO_CMAP
        self._cycle_index = None
        self._num_cycles = 1
        self._cycle_infos = []
        # Kept side of the clip per axis, set by the +/- view buttons so the
        # cut face points toward the camera.
        self._clip_sign = {"X": 1, "Y": 1, "Z": 1}
        # Camera is fitted only on first render, file switch and view buttons.
        self._camera_needs_reset = True

        self._edge_color = "black"
        self._arrow_color = "dimgray"
        self._legend_text_color = "black"

        # Background clip state: one worker at a time, one pending slot,
        # results validated against the current key and cached.
        self._clip_thread = None
        self._pending_clip_request = None
        self._clip_busy_cursor_active = False
        self._active_clip_key = None
        self._full_mesh_for_clip = None  # worker-only copy, never touched by the GUI
        self._mesh_generation = 0
        self._clipped_mesh_cache = None
        self._clipped_mesh_cache_key = None
        self._redraw_scheduled = False

        self._build_ui()
        self._load_mesh()
        self._on_axis_changed()

    def closeEvent(self, event):
        if self._clip_thread is not None:
            self._clip_thread.succeeded.disconnect(self._on_clip_succeeded)
            self._clip_thread.failed.disconnect(self._on_clip_failed)
            self._clip_thread.wait()
        self._clear_busy_cursor()
        self.plotter.close()
        super().closeEvent(event)

    # ---------- UI construction ----------

    @staticmethod
    def _plain_button(text=""):
        # Never a default button: Enter in Min/Max must not trigger it.
        btn = QPushButton(text)
        btn.setAutoDefault(False)
        btn.setDefault(False)
        return btn

    def _build_ui(self):
        self.setWindowTitle("Field Viewer")
        self.resize(1300, 750)
        main_layout = QVBoxLayout(self)
        controls_layout = QHBoxLayout()

        if len(self.file_paths) > 1:
            file_group = QGroupBox("Result File")
            file_layout = QVBoxLayout()
            self.file_combo = QComboBox()
            self.file_combo.addItems([self._file_labels[p] for p in self._visible_paths])
            self.file_combo.currentIndexChanged.connect(self._on_file_changed)
            file_layout.addWidget(self.file_combo)
            hidden_count = len(self.file_paths) - len(self._final_paths)
            if hidden_count > 0:
                self.include_iterations_cb = QCheckBox(f"Include AMR iterations ({hidden_count} more)")
                self.include_iterations_cb.toggled.connect(self._on_include_iterations_toggled)
                file_layout.addWidget(self.include_iterations_cb)
            else:
                self.include_iterations_cb = None
            file_layout.addStretch()
            file_group.setLayout(file_layout)
            controls_layout.addWidget(file_group, 1)
        else:
            self.file_combo = None
            self.include_iterations_cb = None

        self.cycle_group = QGroupBox("Cycle")
        cycle_layout = QVBoxLayout()
        self.cycle_combo = QComboBox()
        self.cycle_combo.currentIndexChanged.connect(self._on_cycle_changed)
        cycle_layout.addWidget(self.cycle_combo)
        cycle_layout.addStretch()
        self.cycle_group.setLayout(cycle_layout)
        self.cycle_group.setVisible(False)
        controls_layout.addWidget(self.cycle_group, 1)

        clip_group = QGroupBox("Clip Plane")
        clip_layout = QVBoxLayout()
        axis_layout = QHBoxLayout()
        self.axis_radio_x = QRadioButton("X")
        self.axis_radio_y = QRadioButton("Y")
        self.axis_radio_z = QRadioButton("Z")
        self.axis_radio_z.setChecked(True)
        self.axis_button_group = QButtonGroup(self)
        for rb in (self.axis_radio_x, self.axis_radio_y, self.axis_radio_z):
            self.axis_button_group.addButton(rb)
            axis_layout.addWidget(rb)
            rb.toggled.connect(self._on_axis_radio_toggled)
        axis_layout.addStretch()
        self.find_max_btn = self._plain_button("Find max.")
        self.find_max_btn.setToolTip(
            "Move the clip plane, along the selected axis, to the position of the "
            "largest value of the selected field (searched on the full, unclipped "
            "model), and enable clipping.")
        self.find_max_btn.setFixedHeight(self.axis_radio_z.sizeHint().height())
        self.find_max_btn.clicked.connect(self._move_slider_to_max)
        axis_layout.addWidget(self.find_max_btn)
        clip_layout.addLayout(axis_layout)

        self.clip_enabled_cb = QCheckBox("Clip enabled")
        self.clip_enabled_cb.toggled.connect(self._on_redraw_needed)
        clip_layout.addWidget(self.clip_enabled_cb)
        self.clip_position_label = QLabel("Position: -")
        clip_layout.addWidget(self.clip_position_label)
        self.clip_slider = QSlider(Qt.Horizontal)
        self.clip_slider.setRange(0, _SLIDER_STEPS)
        self.clip_slider.setValue(_SLIDER_STEPS // 2)
        self.clip_slider.valueChanged.connect(self._on_clip_slider_changed)
        self.clip_slider.sliderReleased.connect(self._schedule_redraw)
        clip_layout.addWidget(self.clip_slider)
        clip_layout.addStretch()
        clip_group.setLayout(clip_layout)
        controls_layout.addWidget(clip_group, 1)

        display_group = QGroupBox("Display")
        display_layout = QVBoxLayout()
        self.opacity_label = QLabel("Opacity: 100%")
        display_layout.addWidget(self.opacity_label)
        self.opacity_slider = QSlider(Qt.Horizontal)
        self.opacity_slider.setRange(0, 100)
        self.opacity_slider.setValue(100)
        self.opacity_slider.valueChanged.connect(self._on_opacity_changed)
        self.opacity_slider.sliderReleased.connect(self._on_opacity_released)
        # Wheel / keys / track clicks change the value without a drag: redraw once they settle.
        self._opacity_settle = QTimer(self)
        self._opacity_settle.setSingleShot(True)
        self._opacity_settle.setInterval(_OPACITY_SETTLE_MS)
        self._opacity_settle.timeout.connect(self._schedule_redraw)
        display_layout.addWidget(self.opacity_slider)

        self.arrow_size_label = QLabel(f"Arrow size: {_ARROW_SIZE_DEFAULT_PERCENT}%")
        self.arrow_size_label.setEnabled(False)
        display_layout.addWidget(self.arrow_size_label)
        self.arrow_size_slider = QSlider(Qt.Horizontal)
        self.arrow_size_slider.setRange(round(_ARROW_SIZE_MIN_PERCENT / _ARROW_SIZE_STEP_PERCENT),
                                        round(_ARROW_SIZE_MAX_PERCENT / _ARROW_SIZE_STEP_PERCENT))
        self.arrow_size_slider.setValue(round(_ARROW_SIZE_DEFAULT_PERCENT / _ARROW_SIZE_STEP_PERCENT))
        self.arrow_size_slider.setEnabled(False)
        self.arrow_size_slider.valueChanged.connect(self._on_arrow_size_changed)
        self.arrow_size_slider.sliderReleased.connect(self._schedule_redraw)
        display_layout.addWidget(self.arrow_size_slider)

        def _swatch_row(attr_name, checkbox):
            swatch = self._plain_button()
            swatch.setFixedSize(18, 18)
            swatch.setToolTip("Choose color")
            self._style_swatch_button(swatch, getattr(self, attr_name))
            swatch.clicked.connect(lambda: self._pick_color(attr_name, swatch))
            row = QHBoxLayout()
            row.addWidget(swatch)
            row.addWidget(checkbox)
            row.addStretch()
            display_layout.addLayout(row)

        self.show_vectors_cb = QCheckBox("Show arrows")
        self.show_vectors_cb.setEnabled(False)
        self.show_vectors_cb.toggled.connect(self._on_redraw_needed)
        _swatch_row("_arrow_color", self.show_vectors_cb)
        self.show_edges_cb = QCheckBox("Overlay mesh")
        self.show_edges_cb.toggled.connect(self._on_redraw_needed)
        _swatch_row("_edge_color", self.show_edges_cb)
        self.show_legend_cb = QCheckBox("Show legend")
        self.show_legend_cb.setChecked(True)
        self.show_legend_cb.toggled.connect(self._on_redraw_needed)
        _swatch_row("_legend_text_color", self.show_legend_cb)
        display_layout.addStretch()
        display_group.setLayout(display_layout)
        controls_layout.addWidget(display_group, 1)

        field_group = QGroupBox("Field")
        field_layout = QVBoxLayout()
        self.array_combo = QComboBox()
        # Long display names; keep the group width stable.
        self.array_combo.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.array_combo.setMinimumContentsLength(16)
        self.array_combo.currentIndexChanged.connect(self._on_array_changed)
        field_layout.addWidget(self.array_combo)
        self.log_scale_cb = QCheckBox("Log color scale")
        self.log_scale_cb.toggled.connect(self._on_log_toggled)
        field_layout.addWidget(self.log_scale_cb)
        # Min: typed value on the linear scale, a dB range below Max in log mode.
        self.clim_min_row = QWidget()
        min_layout = QHBoxLayout(self.clim_min_row)
        min_layout.setContentsMargins(0, 0, 0, 0)
        min_layout.addWidget(QLabel("Min:"))
        self.clim_min_edit = QLineEdit()
        self.clim_min_edit.editingFinished.connect(self._on_redraw_needed)
        min_layout.addWidget(self.clim_min_edit)
        field_layout.addWidget(self.clim_min_row)
        self.log_range_row = QWidget()
        range_layout = QHBoxLayout(self.log_range_row)
        range_layout.setContentsMargins(0, 0, 0, 0)
        range_layout.addWidget(QLabel("Min:"))
        self.log_range_combo = QComboBox()
        for db in _LOG_RANGE_DB_CHOICES:
            self.log_range_combo.addItem(f"-{db} dB", db)
        self.log_range_combo.setCurrentIndex(self.log_range_combo.findData(_LOG_RANGE_DB_DEFAULT))
        self.log_range_combo.setToolTip(
            "Lowest value shown, in dB below Max (field amplitudes such as |E| "
            "use 20*log10, power-like quantities 10*log10).")
        self.log_range_combo.currentIndexChanged.connect(self._on_redraw_needed)
        range_layout.addWidget(self.log_range_combo, 1)
        field_layout.addWidget(self.log_range_row)
        self.log_range_row.setVisible(False)  # shown by _sync_log_widgets
        max_layout = QHBoxLayout()
        max_layout.addWidget(QLabel("Max:"))
        self.clim_max_edit = QLineEdit()
        self.clim_max_edit.editingFinished.connect(self._on_redraw_needed)
        max_layout.addWidget(self.clim_max_edit)
        field_layout.addLayout(max_layout)
        self.clim_reset_btn = self._plain_button("Reset range to data")
        self.clim_reset_btn.clicked.connect(self._on_clim_reset_clicked)
        field_layout.addWidget(self.clim_reset_btn)
        field_layout.addStretch()
        field_group.setLayout(field_layout)
        controls_layout.addWidget(field_group, 1)

        view_group = QGroupBox("View")
        view_grid = QGridLayout()
        for col, axis in enumerate(("X", "Y", "Z")):
            for row, sign in enumerate((1, -1)):
                btn = self._plain_button(f"{'+' if sign > 0 else '-'}{axis}")
                btn.setToolTip(f"Look along the {axis} axis from the "
                               f"{'positive' if sign > 0 else 'negative'} side")
                btn.setFixedHeight(self.axis_radio_z.sizeHint().height())
                btn.clicked.connect(lambda checked=False, a=axis, s=sign: self._set_view(a, s))
                view_grid.addWidget(btn, row, col)
        view_layout = QVBoxLayout()
        view_layout.addLayout(view_grid)
        self.orthographic_cb = QCheckBox("Parallel projection")
        self.orthographic_cb.setToolTip(
            "Parallel projection: no size distortion by distance from the camera. "
            "Always on for the +/-X/Y/Z views; check this to use it for a freely "
            "rotated view too.")
        self.orthographic_cb.toggled.connect(self._on_orthographic_toggled)
        view_layout.addWidget(self.orthographic_cb)
        self.slice_only_cb = QCheckBox("2D plane only")
        self.slice_only_cb.setToolTip(
            "Show only the flat cross-section where the clip plane cuts the model, "
            "instead of the remaining 3D solid. Requires Clip enabled.")
        self.slice_only_cb.toggled.connect(self._on_redraw_needed)
        view_layout.addWidget(self.slice_only_cb)
        view_layout.addStretch()
        view_group.setLayout(view_layout)
        controls_layout.addWidget(view_group, 1)

        main_layout.addLayout(controls_layout)

        self.warning_label = QLabel("")
        self.warning_label.setWordWrap(True)
        self.warning_label.setStyleSheet("color: #b00000;")
        main_layout.addWidget(self.warning_label)

        if self._off_screen:
            # QtInteractor never gets a GL context unless shown (renders
            # black); a plain off-screen Plotter has the same API.
            self.plotter = pv.Plotter(off_screen=True, window_size=_SCREENSHOT_WINDOW_SIZE)
        else:
            self.plotter = QtInteractor(self)
            main_layout.addWidget(self.plotter, 1)
            # Right *drag* navigates; a right *click* opens the menu (eventFilter).
            self.plotter.setContextMenuPolicy(Qt.PreventContextMenu)
            self.plotter.installEventFilter(self)

        QShortcut(QKeySequence.Copy, self).activated.connect(self._copy_view)

    def _copy_view(self):
        if not self._off_screen:
            QApplication.clipboard().setPixmap(self.plotter.grab())

    def _show_plotter_context_menu(self, pos):
        menu = QMenu(self)
        menu.addAction("Copy to Clipboard")
        if menu.exec(self.plotter.mapToGlobal(pos)):
            self._copy_view()


    # ---------- Navigation (mouse style, keys) ----------

    def set_nav_style(self, style):
        """Switch the mouse preset; unknown ids are ignored."""
        if style in NAV_STYLES:
            self.nav_style = style

    def _mapped_press(self, button, mods):
        """(button, modifiers) VTK's trackball should see for a press."""
        left = button == Qt.LeftButton
        if left and mods in (Qt.ShiftModifier, Qt.AltModifier):
            return Qt.MiddleButton, Qt.NoModifier  # pan
        if self.nav_style == "emstudio":
            if button == Qt.RightButton or (left and mods == Qt.ControlModifier):
                return Qt.LeftButton, Qt.NoModifier  # orbit
        return button, mods

    def eventFilter(self, obj, event):
        if obj is not getattr(self, "plotter", None):
            return super().eventFilter(obj, event)
        etype = event.type()
        if etype in (QEvent.MouseButtonPress, QEvent.MouseButtonDblClick):
            if event.button() == Qt.RightButton:
                self._right_press_pos = event.position().toPoint()
            mods = event.modifiers() & ~Qt.KeypadModifier
            button, new_mods = self._mapped_press(event.button(), mods)
            if (button, new_mods) != (event.button(), mods):
                self.plotter.mousePressEvent(QMouseEvent(
                    etype, event.position(), event.globalPosition(),
                    button, button, new_mods))
                return True
            return False
        if etype == QEvent.MouseButtonRelease and event.button() == Qt.RightButton:
            self.plotter.mouseReleaseEvent(event)
            start, self._right_press_pos = self._right_press_pos, None
            pos = event.position().toPoint()
            if start is not None and (pos - start).manhattanLength() < _CLICK_SLOP_PX:
                QTimer.singleShot(0, lambda p=pos: self._show_plotter_context_menu(p))
            return True
        if etype == QEvent.Wheel and self.nav_style == "emstudio" \
                and not event.modifiers() & Qt.ControlModifier:
            self._orbit_by_wheel(event)
            return True
        if etype == QEvent.KeyPress:
            self._handle_key(event)
            return True  # never reach VTK's own key bindings
        if etype == QEvent.KeyRelease:
            return True
        return False

    def _orbit_by_wheel(self, event):
        """EMStudio style: scroll orbits like the Layout 3D view."""
        pixel = event.pixelDelta()
        angle = event.angleDelta()
        if not pixel.isNull():
            dx, dy = pixel.x() * 0.35, pixel.y() * 0.35
        else:
            dx, dy = angle.x() * 0.08, angle.y() * 0.08
        if not dx and not dy:
            return
        cam = self.plotter.camera
        cam.Azimuth(-dx)
        cam.Elevation(-dy)
        cam.OrthogonalizeViewUp()
        self.plotter.reset_camera_clipping_range()
        self.plotter.render()

    def _pan_camera(self, fx, fy):
        """Move the view by a fraction of its half height (right / up positive)."""
        cam = self.plotter.camera
        pos = np.array(cam.position, dtype=float)
        focal = np.array(cam.focal_point, dtype=float)
        direction = focal - pos
        dist = float(np.linalg.norm(direction))
        if dist <= 0.0:
            return
        direction /= dist
        up = np.array(cam.up, dtype=float)
        right = np.cross(direction, up)
        if np.linalg.norm(right) == 0.0:
            return
        right /= np.linalg.norm(right)
        up = np.cross(right, direction)
        half = cam.parallel_scale if cam.parallel_projection \
            else dist * np.tan(np.radians(cam.view_angle) / 2.0)
        shift = (right * fx + up * fy) * half
        cam.position = tuple(pos + shift)
        cam.focal_point = tuple(focal + shift)
        self.plotter.render()

    def _step_clip(self, steps):
        self.clip_slider.setValue(self.clip_slider.value() + steps * (_SLIDER_STEPS // 50))
        if not self.clip_enabled_cb.isChecked():
            self.clip_enabled_cb.setChecked(True)

    def _reset_view_camera(self):
        self.plotter.reset_camera()
        self.plotter.render()

    def _handle_key(self, event):
        """Viewer keys (see module doc); True if the key did something."""
        key = event.key()
        mods = event.modifiers() & ~Qt.KeypadModifier
        shift = mods == Qt.ShiftModifier
        if mods not in (Qt.NoModifier, Qt.ShiftModifier):
            return False
        axis = {Qt.Key_X: "X", Qt.Key_Y: "Y", Qt.Key_Z: "Z"}.get(key)
        if axis:
            self._set_view(axis, -1 if shift else 1)
            return True
        if shift and key not in (Qt.Key_Plus,):
            return False
        cam = self.plotter.camera
        actions = {
            Qt.Key_R: self._reset_view_camera,
            Qt.Key_F: self._reset_view_camera,
            Qt.Key_Home: self._reset_view_camera,
            Qt.Key_I: lambda: (self.plotter.view_isometric(), self.plotter.render()),
            Qt.Key_Plus: lambda: (cam.Zoom(1.15), self.plotter.render()),
            Qt.Key_Equal: lambda: (cam.Zoom(1.15), self.plotter.render()),
            Qt.Key_Minus: lambda: (cam.Zoom(1.0 / 1.15), self.plotter.render()),
            Qt.Key_Left: lambda: self._pan_camera(-0.2, 0.0),
            Qt.Key_Right: lambda: self._pan_camera(0.2, 0.0),
            Qt.Key_Up: lambda: self._pan_camera(0.0, 0.2),
            Qt.Key_Down: lambda: self._pan_camera(0.0, -0.2),
            Qt.Key_O: self.orthographic_cb.toggle,
            Qt.Key_M: self._move_slider_to_max,
            Qt.Key_A: lambda: self.show_vectors_cb.isEnabled() and self.show_vectors_cb.toggle(),
            Qt.Key_PageUp: lambda: self._step_clip(1),
            Qt.Key_PageDown: lambda: self._step_clip(-1),
        }
        action = actions.get(key)
        if action is None:
            return False
        action()
        return True

    # ---------- Result file / cycle pickers ----------

    def _on_file_changed(self, index):
        if 0 <= index < len(self._visible_paths):
            self._switch_to_file(self._visible_paths[index])

    def _switch_to_file(self, path):
        """Load another result file. With the same model extent the camera (view, zoom,
        projection) and the clip plane position stay; otherwise both are reset."""
        if path == self.file_path:
            return
        old_bounds = self._full_mesh.bounds if self._full_mesh is not None else None
        old_clip = self._slider_value_to_position() if old_bounds is not None else None
        self.file_path = path
        self._load_error = None
        self.warning_label.setText("")
        self._cycle_index = None
        self._load_mesh(keep_field=True)
        same_extent = (old_bounds is not None and self._full_mesh is not None
                       and _same_extent(old_bounds, self._full_mesh.bounds))
        self._camera_needs_reset = not same_extent
        if not same_extent:
            self._on_axis_changed()
            return
        self.clip_slider.blockSignals(True)
        self.clip_slider.setValue(self._native_to_slider_value(self._current_axis, old_clip))
        self.clip_slider.blockSignals(False)
        self._update_clip_position_label()
        self._schedule_redraw()

    def _on_cycle_changed(self, index):
        if index < 0 or index == self._cycle_index:
            return
        self._cycle_index = index
        self._load_mesh(preserve_selection=True)
        self._schedule_redraw()

    def _on_include_iterations_toggled(self, checked):
        self._visible_paths = self.file_paths if checked else self._final_paths
        target = self.file_path if self.file_path in self._visible_paths else self._visible_paths[0]
        self.file_combo.blockSignals(True)
        self.file_combo.clear()
        self.file_combo.addItems([self._file_labels[p] for p in self._visible_paths])
        self.file_combo.setCurrentIndex(self._visible_paths.index(target))
        self.file_combo.blockSignals(False)
        self._switch_to_file(target)

    def select_file(self, path):
        """Programmatic file choice (CLI / EMStudio); AMR copies are revealed
        if needed. False if path is not a candidate."""
        path = os.path.abspath(path)
        if path not in self.file_paths:
            return False
        if path not in self._visible_paths and self.include_iterations_cb is not None:
            self.include_iterations_cb.setChecked(True)
        if self.file_combo is not None and path in self._visible_paths:
            self.file_combo.setCurrentIndex(self._visible_paths.index(path))
        return path == self.file_path

    def select_cycle(self, cycle_1based):
        """Programmatic cycle choice (CLI / EMStudio); False if out of range."""
        if not 1 <= cycle_1based <= self._num_cycles:
            return False
        self.cycle_combo.setCurrentIndex(cycle_1based - 1)
        return True

    # ---------- Mesh loading ----------

    def _current_array(self):
        data = self.array_combo.currentData()
        return data if isinstance(data, str) and data else None

    def _select_array(self, name):
        idx = self.array_combo.findData(name)
        if idx >= 0:
            self.array_combo.setCurrentIndex(idx)
        return idx >= 0

    def _populate_array_combo(self):
        self.array_combo.blockSignals(True)
        self.array_combo.clear()
        last_group = None
        for e in field_io.field_entries(self._full_mesh):
            if last_group is not None and e.group != last_group:
                self.array_combo.insertSeparator(self.array_combo.count())
            last_group = e.group
            self.array_combo.addItem(e.display, e.name)
            self.array_combo.setItemData(self.array_combo.count() - 1, e.tooltip, Qt.ToolTipRole)
            self._entries[e.name] = e
        self.array_combo.blockSignals(False)

    def _refresh_cycle_combo(self):
        labels = [c["label"] for c in self._cycle_infos]
        if len(labels) != self._num_cycles:
            labels = [f"Cycle {i + 1}" for i in range(self._num_cycles)]
            self._cycle_infos = [{"label": s, "geometry": None} for s in labels]
        self.cycle_combo.blockSignals(True)
        self.cycle_combo.clear()
        self.cycle_combo.addItems(labels)
        self.cycle_combo.setCurrentIndex(self._cycle_index)
        self.cycle_combo.blockSignals(False)
        self.cycle_group.setVisible(self._num_cycles > 1)

    def _load_mesh(self, preserve_selection=False, keep_field=False):
        """Load self.file_path / self._cycle_index (spec 5.1).

        preserve_selection: cycle switch of the same file. keep_field: another file.
        Both keep the Field pane (field, Log, dB range, Min/Max) when the new data has
        the selected field; otherwise it is reset to the defaults."""
        self.setWindowTitle(f"Field Viewer - {self._file_labels.get(self.file_path, self.file_path)}")
        self._mesh_generation += 1
        self._clipped_mesh_cache = None
        self._clipped_mesh_cache_key = None
        self._full_mesh_for_clip = None
        self._pending_clip_request = None
        previous_array = self._current_array() if (preserve_selection or keep_field) else None
        if not preserve_selection:
            self._cycle_infos = field_io.cycle_infos(self.file_path, self.source)
        try:
            self._full_mesh, self._cycle_index, self._num_cycles = field_io.load(
                self.file_path, self._cycle_index)
        except Exception as exc:
            self._load_error = str(exc)
            self.warning_label.setText(f"Failed to load {self.file_path}: {exc}")
            self._full_mesh = None
            return
        self.warning_label.setText("")

        if self.source is None:
            self.source = field_io.guess_source_from_names(self._full_mesh.point_data.keys())
        self._scale_um = field_io.scale_to_um(self.source, self._full_mesh.bounds)
        field_io.attach_derived(self._full_mesh, self.source)

        available = field_io.visible_point_arrays(self._full_mesh)
        if not available and self._cycle_index < len(self._cycle_infos):
            # Geometry-only cycle (e.g. Palace AMR indicator dump): not an error.
            self._cycle_infos[self._cycle_index] = {
                "label": f"geometry (cycle {self._cycle_index + 1})", "geometry": True}
        self._refresh_cycle_combo()
        self._entries = {}
        self._populate_array_combo()

        if previous_array is not None and previous_array in available:
            # Cycle or file switch: keep field, Log, dB range and Min/Max.
            self.array_combo.blockSignals(True)
            self._select_array(previous_array)
            self.array_combo.blockSignals(False)
            self._update_vector_checkbox_state()
            return

        default_array, cmap, log_scale = field_io.pick_default(self._full_mesh, self.source)
        self._current_cmap = cmap
        self.log_scale_cb.blockSignals(True)
        self.log_scale_cb.setChecked(log_scale)
        self.log_scale_cb.blockSignals(False)
        self._sync_log_widgets()
        if default_array is not None:
            self.array_combo.blockSignals(True)
            self._select_array(default_array)
            self.array_combo.blockSignals(False)
        self._reset_clim_range()
        self._update_vector_checkbox_state()

    def replaced_by(self, file_paths, source):
        """New window for another run at this window's place; closes this one."""
        new = FieldViewerWindow(file_paths, source, icon_path=self._icon_path,
                                nav_style=self.nav_style)
        new.setGeometry(self.geometry())
        new.show()
        new.plotter.setFocus()
        self.close()
        return new

    # ---------- Axis / clip plane ----------

    def _on_axis_radio_toggled(self, checked):
        if checked:
            self._on_axis_changed()

    def _current_axis_name(self):
        if self.axis_radio_x.isChecked():
            return "X"
        if self.axis_radio_y.isChecked():
            return "Y"
        return "Z"

    def _on_axis_changed(self):
        self._current_axis = self._current_axis_name()
        self.clip_slider.blockSignals(True)
        self.clip_slider.setValue(_SLIDER_STEPS // 2)
        self.clip_slider.blockSignals(False)
        self._schedule_redraw()

    def _axis_bounds(self, axis):
        lo_idx, hi_idx = _AXIS_BOUNDS_INDEX[axis]
        return self._full_mesh.bounds[lo_idx], self._full_mesh.bounds[hi_idx]

    def _slider_value_to_position(self):
        """Slider step -> native coordinate on the current axis."""
        if self._full_mesh is None:
            return 0.0
        lo, hi = self._axis_bounds(self._current_axis)
        return lo + (self.clip_slider.value() / _SLIDER_STEPS) * (hi - lo)

    def _native_to_slider_value(self, axis, position_native):
        lo, hi = self._axis_bounds(axis)
        fraction = (position_native - lo) / (hi - lo) if hi > lo else 0.5
        return round(min(max(fraction, 0.0), 1.0) * _SLIDER_STEPS)

    def _position_um_to_slider_value(self, axis, position_um):
        if self._full_mesh is None:
            return _SLIDER_STEPS // 2
        return self._native_to_slider_value(axis, position_um / self._scale_um)

    def _on_redraw_needed(self, _value=None):
        self._schedule_redraw()

    def _pick_color(self, attr_name, button):
        color = QColorDialog.getColor(QColor(getattr(self, attr_name)), self, "Choose color")
        if color.isValid():
            setattr(self, attr_name, color.name())
            self._style_swatch_button(button, color.name())
            self._schedule_redraw()

    @staticmethod
    def _style_swatch_button(button, color_hex):
        button.setStyleSheet(f"background-color: {color_hex}; border: 1px solid #666;")

    def _on_clip_slider_changed(self, _value=None):
        # Live label while dragging; redraw on release or keyboard changes.
        self._update_clip_position_label()
        if not self.clip_slider.isSliderDown():
            self._schedule_redraw()

    def _on_opacity_changed(self, value):
        # The label follows the slider; the 3D view is redrawn only on release
        # (drag) or after the value settles (wheel, keys, track click).
        self.opacity_label.setText(f"Opacity: {value}%")
        if not self.opacity_slider.isSliderDown():
            self._opacity_settle.start()

    def _on_opacity_released(self):
        self._opacity_settle.stop()
        self._schedule_redraw()

    def _on_arrow_size_changed(self, value):
        self.arrow_size_label.setText(f"Arrow size: {value * _ARROW_SIZE_STEP_PERCENT:g}%")
        if not self.arrow_size_slider.isSliderDown():
            self._schedule_redraw()

    # ---------- Axis views ----------

    def _set_view(self, axis, sign):
        """Look along +/-axis at the mesh centre, parallel projection, re-fit;
        also flips the clip's kept side so the cut face faces the camera."""
        if self._full_mesh is None:
            return
        self._clip_sign[axis] = sign
        center = np.array(self._full_mesh.center)
        direction = np.array(_AXIS_NORMAL[axis]) * sign
        distance = max(self._full_mesh.length, 1e-12) * 3.0
        self.plotter.camera_position = [tuple(center + direction * distance),
                                        tuple(center), _VIEW_UP[axis]]
        self.plotter.enable_parallel_projection()
        self.orthographic_cb.blockSignals(True)
        self.orthographic_cb.setChecked(True)
        self.orthographic_cb.blockSignals(False)
        self.plotter.reset_camera()
        self._schedule_redraw()

    def _on_orthographic_toggled(self, checked):
        if checked:
            self.plotter.enable_parallel_projection()
        else:
            self.plotter.disable_parallel_projection()
        self.plotter.render()

    def _move_slider_to_max(self):
        """Put the plane at the selected field's maximum (full mesh) and clip."""
        name = self._current_array()
        if self._full_mesh is None or not name or name not in self._full_mesh.point_data:
            return
        mags = field_io.array_magnitudes(self._full_mesh[name])
        if mags.size == 0:
            return
        pos = self._full_mesh.points[int(np.argmax(mags)), _AXIS_POINT_INDEX[self._current_axis]]
        self.clip_slider.blockSignals(True)
        self.clip_slider.setValue(self._native_to_slider_value(self._current_axis, pos))
        self.clip_slider.blockSignals(False)
        self.clip_enabled_cb.blockSignals(True)
        self.clip_enabled_cb.setChecked(True)
        self.clip_enabled_cb.blockSignals(False)
        self._schedule_redraw()

    # ---------- Field picker ----------

    def _on_array_changed(self, _index=None):
        self._reset_clim_range()
        self._update_vector_checkbox_state()
        self._schedule_redraw()

    def _is_vector(self, name):
        return (self._full_mesh is not None and bool(name)
                and name in self._full_mesh.point_data
                and self._full_mesh.point_data[name].ndim > 1)

    def _update_vector_checkbox_state(self):
        # Checked state is kept while disabled, restored for the next vector.
        is_vector = self._is_vector(self._current_array())
        self.show_vectors_cb.setEnabled(is_vector)
        self.show_vectors_cb.setToolTip(
            "Overlay direction arrows for this vector field" if is_vector
            else "Only available when the selected Field is a vector array "
                 "(e.g. E Re, B Re, S)")
        self.arrow_size_label.setEnabled(is_vector)
        self.arrow_size_slider.setEnabled(is_vector)

    def _reset_clim_range(self):
        """Min/Max from the full, unclipped mesh's range of the selected field."""
        name = self._current_array()
        if self._full_mesh is None or not name or name not in self._full_mesh.point_data:
            self.clim_min_edit.setText("")
            self.clim_max_edit.setText("")
            return
        mags = field_io.array_magnitudes(self._full_mesh[name])
        if mags.size == 0:
            self.clim_min_edit.setText("")
            self.clim_max_edit.setText("")
            return
        self.clim_min_edit.setText(f"{mags.min():.6g}")
        self.clim_max_edit.setText(f"{mags.max():.6g}")

    def _on_clim_reset_clicked(self):
        self._reset_clim_range()
        self._schedule_redraw()

    def _sync_log_widgets(self):
        """Min input (linear) or dB range dropdown (log), per the Log checkbox."""
        log = self.log_scale_cb.isChecked()
        self.clim_min_row.setVisible(not log)
        self.log_range_row.setVisible(log)

    def _on_log_toggled(self, _checked):
        self._sync_log_widgets()
        self._schedule_redraw()

    def set_log_range_db(self, db):
        """Select the log range (adds an entry for values outside the list)."""
        idx = self.log_range_combo.findData(db)
        if idx < 0:
            self.log_range_combo.addItem(f"-{db:g} dB", db)
            idx = self.log_range_combo.count() - 1
        self.log_range_combo.setCurrentIndex(idx)

    def _log_range_per_decade(self):
        name = self._current_array()
        return 20.0 if name and field_io.is_amplitude_array(name) else 10.0

    def _get_clim(self):
        """(min, max), or None = auto-scale (invalid / min >= max).

        Log mode: Max from the field, Min = Max reduced by the dB range."""
        try:
            hi = float(self.clim_max_edit.text())
            if self.log_scale_cb.isChecked():
                db = float(self.log_range_combo.currentData())
                lo = hi / 10.0 ** (db / self._log_range_per_decade()) if hi > 0 else hi
            else:
                lo = float(self.clim_min_edit.text())
        except (ValueError, TypeError):
            return None
        return (lo, hi) if lo < hi else None

    # ---------- Vector arrows ----------

    def _add_vector_glyphs(self, mesh, name):
        """Arrows sized from log10(|v|) to a fraction of the bounding-box
        diagonal; decimation follows the arrow size (spec 4.5)."""
        mags = np.linalg.norm(mesh.point_data[name], axis=1)
        top = mags.max() if mags.size else 0.0
        if top <= 0:
            self.warning_label.setText("Vector arrows: the field is zero everywhere shown.")
            return None
        fraction = (self.arrow_size_slider.value() * _ARROW_SIZE_STEP_PERCENT) / 100.0
        diagonal = mesh.length or 1.0
        keep = _decimate_indices(np.asarray(mesh.points), diagonal * fraction * _ARROW_DECIMATION_RATIO, mags)
        mags = mags[keep]
        log_mag = np.log10(np.clip(mags, top * 1e-6, None))
        lo, hi = log_mag.min(), log_mag.max()
        normalized = (log_mag - lo) / (hi - lo) if hi > lo else np.ones_like(log_mag)
        # Glyph a separate point cloud, so no helper array ever lands on the
        # displayed mesh (and the field list).
        cloud = pv.PolyData(np.asarray(mesh.points)[keep])
        cloud.point_data[name] = np.asarray(mesh.point_data[name])[keep]
        cloud.point_data[_GLYPH_SCALE_KEY] = diagonal * fraction * (
            _ARROW_MIN_LENGTH_RATIO + (1.0 - _ARROW_MIN_LENGTH_RATIO) * normalized)
        try:
            glyphs = cloud.glyph(orient=name, scale=_GLYPH_SCALE_KEY, factor=1.0, tolerance=None)
        except Exception as exc:
            self.warning_label.setText(f"Vector arrows failed: {exc}")
            return None
        return self.plotter.add_mesh(glyphs, color=self._arrow_color, reset_camera=False)

    # ---------- Redraw ----------

    def _update_clip_position_label(self):
        pos_um = self._slider_value_to_position() * self._scale_um
        self.clip_position_label.setText(f"Position: {pos_um:.4g} um ({self._current_axis})")

    def _schedule_redraw(self, *_args):
        """Coalesce all changes before the event loop turns into one redraw."""
        if not self._redraw_scheduled:
            self._redraw_scheduled = True
            QTimer.singleShot(0, self._perform_scheduled_redraw)

    def _perform_scheduled_redraw(self):
        self._redraw_scheduled = False
        self._redraw()

    def _redraw(self):
        if self._full_mesh is None:
            return
        self._update_clip_position_label()
        if not self.clip_enabled_cb.isChecked():
            self._pending_clip_request = None
            self._apply_display_mesh(self._full_mesh)
            return
        key = self._current_clip_key()
        if key == self._clipped_mesh_cache_key:
            self._pending_clip_request = None
            self._apply_display_mesh(self._clipped_mesh_cache)
            return
        self._request_clip(*key)

    def _request_clip(self, *key):
        if self._clip_thread is not None and self._clip_thread.isRunning():
            self._pending_clip_request = None if key == self._active_clip_key else key
            return
        self._start_clip_thread(*key)

    def _start_clip_thread(self, *key):
        self._pending_clip_request = None
        self._active_clip_key = key
        if not self._clip_busy_cursor_active:
            self.setCursor(Qt.WaitCursor)  # this window only, not app-wide
            self._clip_busy_cursor_active = True
        if self._full_mesh_for_clip is None:
            self._full_mesh_for_clip = self._full_mesh.copy()
        thread = _ClipWorker(self._full_mesh_for_clip, *key)
        thread.succeeded.connect(self._on_clip_succeeded)
        thread.failed.connect(self._on_clip_failed)
        self._clip_thread = thread
        thread.start()

    def _retire_clip_thread(self):
        # Qt may still consider the thread running right after it emitted.
        self._clip_thread.wait()
        self._clip_thread = None

    def _on_clip_succeeded(self, clipped_mesh, *key):
        self._retire_clip_thread()
        if key == self._current_clip_key():
            self.warning_label.setText("")
            self._clipped_mesh_cache = clipped_mesh
            self._clipped_mesh_cache_key = key
            self._apply_display_mesh(clipped_mesh)
        self._maybe_start_pending_clip()

    def _on_clip_failed(self, message, *key):
        self._retire_clip_thread()
        if key == self._current_clip_key():
            self.warning_label.setText(f"Clip failed (showing the unclipped model): {message}")
            self._apply_display_mesh(self._full_mesh)
        self._maybe_start_pending_clip()

    def _maybe_start_pending_clip(self):
        if self._pending_clip_request is not None:
            self._start_clip_thread(*self._pending_clip_request)
        else:
            self._clear_busy_cursor()

    def _current_clip_key(self):
        if not self.clip_enabled_cb.isChecked():
            return None
        return (self._current_axis, self._slider_value_to_position(),
                self._clip_sign.get(self._current_axis, 1),
                self.slice_only_cb.isChecked(), self._mesh_generation)

    def clip_settled(self):
        """True once the requested clip (if any) is applied (headless wait)."""
        running = self._clip_thread is not None and self._clip_thread.isRunning()
        key = self._current_clip_key()
        return not running and (key is None or self._clipped_mesh_cache_key == key)

    def _clear_busy_cursor(self):
        if self._clip_busy_cursor_active:
            self.unsetCursor()
            self._clip_busy_cursor_active = False

    def _apply_display_mesh(self, display_mesh):
        name = self._current_array()
        if self._mesh_actor is not None:
            self.plotter.remove_actor(self._mesh_actor, render=False)
            self._mesh_actor = None
        opacity = self.opacity_slider.value() / 100.0
        show_edges = self.show_edges_cb.isChecked()

        if name and name in display_mesh.point_data and display_mesh.n_points > 0:
            mags = field_io.array_magnitudes(display_mesh[name])
            clim = self._get_clim()
            # Log needs a positive lower bound; fall back to linear silently.
            lower = clim[0] if clim is not None else (mags.min() if mags.size else 0)
            use_log = bool(self.log_scale_cb.isChecked() and lower > 0)
            entry = self._entries.get(name)
            rng = clim if clim is not None else (
                (float(mags.min()), float(mags.max())) if mags.size else (0.0, 1.0))
            cmap, clim, use_log = _colormap_for(self._current_cmap, rng, use_log)
            self._mesh_actor = self.plotter.add_mesh(
                display_mesh, scalars=name, cmap=cmap,
                show_edges=show_edges, edge_color=self._edge_color,
                log_scale=use_log, clim=clim, opacity=opacity,
                show_scalar_bar=self.show_legend_cb.isChecked(),
                scalar_bar_args={"title": entry.title if entry else name,
                                 "color": self._legend_text_color},
                reset_camera=False)
        elif display_mesh.n_points > 0:
            self._mesh_actor = self.plotter.add_mesh(
                display_mesh, color="lightgrey", opacity=opacity, show_edges=show_edges,
                edge_color=self._edge_color, reset_camera=False)

        if self._vector_actor is not None:
            self.plotter.remove_actor(self._vector_actor, render=False)
            self._vector_actor = None
        if (self.show_vectors_cb.isChecked() and self.show_vectors_cb.isEnabled()
                and name and name in display_mesh.point_data
                and display_mesh.point_data[name].ndim > 1):
            self._vector_actor = self._add_vector_glyphs(display_mesh, name)

        if self._camera_needs_reset:
            self.plotter.reset_camera()
            self._camera_needs_reset = False
        self.plotter.render()


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------

def _resolve_files(file_path, run_path, source):
    if file_path:
        path = os.path.abspath(field_io.openems_abs_path(file_path))
        # An openEMS dump: offer the run's other frequencies / excitations too.
        run_dir = field_io.openems_run_dir(path)
        if run_dir and source in (None, field_io.OPENEMS):
            files = field_io.discover(run_dir, field_io.OPENEMS)
            if path in files:
                return [path] + [f for f in files if f != path]
        return [path]
    if run_path:
        return field_io.discover(run_path, source or field_io.PALACE)
    return []


def _apply_cli_options(window, args, die):
    """Drive the same widgets a user would (spec 7)."""
    if args.select_file and not window.select_file(args.select_file):
        print(f"Warning: --select-file {args.select_file} is not among the result files; "
              f"showing {window.file_path}", file=sys.stderr)
    if window._full_mesh is None:
        die(window._load_error or "failed to load the field-result file")
    if args.cycle is not None and not window.select_cycle(args.cycle):
        die(f"--cycle {args.cycle} out of range - this file has {window._num_cycles} cycle(s)")
    mesh = window._full_mesh

    if args.array:
        name = field_io.resolve_array_name(mesh, args.array)
        if name is None:
            available = ", ".join(field_io.visible_point_arrays(mesh))
            die(f"--array {args.array!r} not found in this file. Available arrays: {available}")
        window._select_array(name)
    elif args.field:
        name = field_io.resolve_field_shorthand(args.field, mesh)
        if name is None:
            die(f"--field {args.field!r} is not available in this file "
                f"(source {window.source or 'unknown'})")
        window._select_array(name)

    if args.log_scale or args.log_range_db is not None:
        window.log_scale_cb.setChecked(True)
    if args.log_range_db is not None:
        name = window._current_array()
        result = field_io.db_range_to_clim(mesh, name, args.log_range_db) if name else None
        if result is None:
            die(f"--log-range-db: array {name!r} has no positive values to scale from")
        window.set_log_range_db(args.log_range_db)
        window._schedule_redraw()

    if args.opacity is not None:
        window.opacity_slider.setValue(round(args.opacity))
    if args.overlay_mesh:
        window.show_edges_cb.setChecked(True)
    if args.arrows:
        window.show_vectors_cb.setChecked(True)
        if not window.show_vectors_cb.isEnabled():
            print("Warning: --arrows requested but the selected array is not a vector "
                  "field; no arrows will be drawn.", file=sys.stderr)
    if args.arrow_size is not None:
        window.arrow_size_slider.setValue(round(args.arrow_size / _ARROW_SIZE_STEP_PERCENT))

    if args.view_axis:
        if args.view_axis == "ISO":
            window.orthographic_cb.setChecked(False)
            window.plotter.view_isometric()
        else:
            window._set_view(args.view_axis[0], 1 if args.view_axis[1] == "+" else -1)
    if args.orthographic:
        window.orthographic_cb.setChecked(True)

    if args.clip_axis:
        {"X": window.axis_radio_x, "Y": window.axis_radio_y,
         "Z": window.axis_radio_z}[args.clip_axis].setChecked(True)
        if args.clip_max:
            window._move_slider_to_max()
        elif args.clip_position is not None:
            window.clip_slider.setValue(
                window._position_um_to_slider_value(args.clip_axis, args.clip_position))
            window.clip_enabled_cb.setChecked(True)


def main(argv=None):
    app = QApplication.instance() or QApplication(sys.argv[:1])
    try:
        app.styleHints().setColorScheme(Qt.ColorScheme.Light)
    except AttributeError:
        pass
    if sys.platform.startswith("win"):
        app.setStyle(QStyleFactory.create("Windows"))

    sources = sorted(field_io.SOURCES)
    parser = argparse.ArgumentParser(description="EMStudio 3D field result viewer")
    parser.add_argument("file_path", nargs="?",
                        help="a .pvd/.pvtu/.vtu (or openEMS *_abs.vtr) to open; "
                             "otherwise use --run-path")
    parser.add_argument("--input", "-i", dest="input_path", help=argparse.SUPPRESS)
    parser.add_argument("--run-path", help="a *_data run directory to search (spec 2)")
    parser.add_argument("--source", choices=sources,
                        help="preset and units (default: palace for --run-path, "
                             "guessed from the arrays for a single file)")
    parser.add_argument("--select-file", help="with --run-path: result file to show first")
    parser.add_argument("--cycle", type=int, help="1-based cycle number")
    parser.add_argument("--field", choices=["e", "b", "s", "temp"],
                        help="E real part, B real part, Poynting vector, temperature")
    parser.add_argument("--array", help="exact array name (overrides --field)")
    parser.add_argument("--log-scale", action="store_true", help="log color scale")
    parser.add_argument("--log-range-db", type=float,
                        help="log scale range: Min this many dB below Max, default 70 "
                             "(implies --log-scale)")
    parser.add_argument("--clip-axis", choices=["X", "Y", "Z"], help="enable clipping on this axis")
    parser.add_argument("--clip-position", type=float, help="plane position in um")
    parser.add_argument("--clip-max", action="store_true", help="plane at the field maximum")
    parser.add_argument("--arrows", action="store_true", help="vector arrows")
    parser.add_argument("--arrow-size", type=float,
                        help=f"arrow size in percent ({_ARROW_SIZE_MIN_PERCENT}-{_ARROW_SIZE_MAX_PERCENT})")
    parser.add_argument("--opacity", type=float, help="opacity percent 0-100")
    parser.add_argument("--overlay-mesh", action="store_true", help="draw cell edges")
    parser.add_argument("--view-axis", choices=["X+", "X-", "Y+", "Y-", "Z+", "Z-", "ISO"])
    parser.add_argument("--orthographic", action="store_true", help="parallel projection")
    parser.add_argument("--screenshot", help="render off-screen to this PNG and exit")
    parser.add_argument("--icon", help="window icon file")
    parser.add_argument("--nav-style", choices=NAV_STYLES, default="setupem",
                        help="mouse navigation preset (default: setupem, VTK trackball)")
    parser.add_argument("--stdin-control", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    if (args.clip_position is not None or args.clip_max) and not args.clip_axis:
        parser.error("--clip-position/--clip-max require --clip-axis")
    if args.clip_position is not None and args.clip_max:
        parser.error("--clip-position and --clip-max are mutually exclusive")
    if args.opacity is not None and not 0 <= args.opacity <= 100:
        parser.error("--opacity must be between 0 and 100")
    if args.arrow_size is not None and not (
            _ARROW_SIZE_MIN_PERCENT <= args.arrow_size <= _ARROW_SIZE_MAX_PERCENT):
        parser.error(f"--arrow-size must be between {_ARROW_SIZE_MIN_PERCENT} "
                     f"and {_ARROW_SIZE_MAX_PERCENT}")
    if args.log_range_db is not None and args.log_range_db <= 0:
        parser.error("--log-range-db must be positive")
    if args.cycle is not None and args.cycle < 1:
        parser.error("--cycle must be a 1-based cycle number (>= 1)")

    source = args.source
    if source is None and args.run_path and not (args.file_path or args.input_path):
        source = field_io.PALACE
    if source is None and field_io.openems_run_dir(args.file_path or args.input_path or ""):
        source = field_io.OPENEMS
    files = _resolve_files(args.file_path or args.input_path, args.run_path, source)
    if not files:
        hint = field_io.SOURCES[source].no_files_hint if source in field_io.SOURCES else ""
        parser.error("no file_path given, and none could be resolved from --run-path. " + hint)

    def die(message):
        print(f"Error: {message}", file=sys.stderr, flush=True)
        sys.exit(1)

    icon_path = _find_icon(args.icon)
    window = FieldViewerWindow(files, source, off_screen=bool(args.screenshot), icon_path=icon_path,
                               nav_style=args.nav_style)
    if window._full_mesh is None and not args.select_file:
        die(window._load_error or "failed to load the field-result file")
    _apply_cli_options(window, args, die)

    if args.screenshot:
        # Wait until the clip is applied, not just until the worker stopped.
        deadline = time.monotonic() + 300
        while True:
            app.processEvents()
            if not window._redraw_scheduled and window.clip_settled():
                break
            time.sleep(0.02)
            if time.monotonic() > deadline:
                print("Warning: timed out waiting for the clip plane; the screenshot "
                      "may not show the requested clip.", file=sys.stderr)
                break
        window.plotter.screenshot(args.screenshot)
        print(f"Saved screenshot to {args.screenshot}", flush=True)
        window.close()
        return 0

    state = {"window": window, "files": files, "source": source}
    if args.stdin_control:
        # EMStudio re-open: {"run_path"|"files", "source", "select_file", "cycle"};
        # {"nav_style": ...} switches the mouse preset.
        listener = _StdinListener()

        def _on_message(msg):
            win = state["window"]
            if "nav_style" in msg:
                win.set_nav_style(msg["nav_style"])
                if set(msg) == {"nav_style"}:
                    return  # style change only: don't raise the window
            src = msg.get("source") or state["source"]
            new_files = [os.path.abspath(f) for f in msg.get("files") or []] or \
                _resolve_files(None, msg.get("run_path"), src)
            if new_files and (new_files != state["files"] or src != state["source"]):
                win = win.replaced_by(new_files, src)
                state.update(window=win, files=new_files, source=src)
            if msg.get("select_file"):
                win.select_file(msg["select_file"])
            if msg.get("cycle"):
                win.select_cycle(int(msg["cycle"]))
            win.showNormal()
            win.raise_()
            win.activateWindow()
            print("field_viewer: raised", flush=True)

        listener.message.connect(_on_message)
        listener.start()
        state["listener"] = listener

    window.show()
    window.plotter.setFocus()  # viewer keys work without clicking first
    app.processEvents()
    print(f"field_viewer: ready ({len(files)} file(s), source {window.source or 'unknown'})",
          flush=True)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
