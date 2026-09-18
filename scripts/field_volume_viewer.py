#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Interactive 3D field volume viewer for EMStudio (separate OS window).

Opened from Layout Field → 3D. Uses PyVista (same FIELD_VIEWER_PYTHON as the
2D slice exporter). Optional pyvistaqt for a nicer Qt window; falls back to
pv.Plotter().show().
"""

from __future__ import annotations

import argparse
import os
import sys


def _die(msg: str, code: int = 1) -> None:
    print(f"field_volume_viewer: {msg}", file=sys.stderr)
    sys.exit(code)


def _find_logo(explicit: str | None) -> str | None:
    if explicit and os.path.isfile(explicit):
        return os.path.abspath(explicit)
    here = os.path.dirname(os.path.abspath(__file__))
    for rel in (
        os.path.join(here, "icons", "logo.png"),
        os.path.join(here, "..", "icons", "logo.png"),
        os.path.join(here, "..", "..", "icons", "logo.png"),
    ):
        p = os.path.normpath(rel)
        if os.path.isfile(p):
            return p
    return None


def _find_icon(explicit: str | None, logo_path: str | None) -> str | None:
    """Prefer .ico for the Windows title-bar / taskbar icon."""
    if explicit and os.path.isfile(explicit):
        return os.path.abspath(explicit)
    here = os.path.dirname(os.path.abspath(__file__))
    for rel in (
        os.path.join(here, "..", "appicon.ico"),
        os.path.join(here, "appicon.ico"),
        os.path.join(here, "..", "..", "appicon.ico"),
        os.path.join(here, "icons", "appicon.ico"),
    ):
        p = os.path.normpath(rel)
        if os.path.isfile(p):
            return p
    return logo_path


def _try_import_pyvista():
    try:
        import pyvista as pv  # noqa: F401
        return pv
    except Exception as exc:  # pragma: no cover
        _die(
            "PyVista is required for Field 3D view. Install with:\n"
            "  pip install pyvista\n"
            "Optional (nicer window): pip install pyvistaqt PySide6\n"
            f"Import error: {exc}"
        )


def _array_names(mesh) -> list[str]:
    names = []
    try:
        names.extend(list(mesh.point_data.keys()))
    except Exception:
        pass
    try:
        names.extend(list(mesh.cell_data.keys()))
    except Exception:
        pass
    seen = set()
    out = []
    for n in names:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def _pick_scalar(names, prefer):
    if prefer:
        for n in names:
            if n.lower() == prefer.lower():
                return n
        for n in names:
            if prefer.lower() in n.lower():
                return n
    for key in ("|E|", "E_abs", "Eabs", "magE", "AbsE", "E_imag", "E_real"):
        for n in names:
            if n.lower() == key.lower():
                return n
    for n in names:
        if "temp" in n.lower():
            return n
    for n in names:
        nl = n.lower()
        if nl.endswith("_x") or nl.endswith("_y") or nl.endswith("_z"):
            continue
        return n
    return names[0] if names else None


def _guess_scale_to_um(bounds) -> float:
    span = max(abs(bounds[1] - bounds[0]), abs(bounds[3] - bounds[2]), abs(bounds[5] - bounds[4]))
    if span <= 0:
        return 1.0e6
    if span < 1e-2:
        return 1.0e6
    if span < 50:
        return 1.0
    if span > 1e4:
        return 1.0e-3
    return 1.0


def _load_mesh(pv, path: str, scale_to_um, quantity: str | None):
    import numpy as np

    if not os.path.isfile(path):
        _die(f"input not found: {path}")
    mesh = pv.read(path)
    if hasattr(mesh, "n_blocks"):
        for i in range(mesh.n_blocks):
            block = mesh[i]
            if block is not None and getattr(block, "n_points", 0) > 0:
                mesh = block
                break
    if mesh.n_points == 0:
        _die("mesh has no points")

    scale = scale_to_um if scale_to_um is not None else _guess_scale_to_um(list(mesh.bounds))
    names = _array_names(mesh)
    scalar = _pick_scalar(names, quantity)
    if not scalar:
        _die(f"no scalar arrays in {path}; arrays={names}")

    if scalar in mesh.cell_data and scalar not in mesh.point_data:
        mesh = mesh.cell_data_to_point_data()

    mesh = mesh.copy(deep=True)
    mesh.points = np.asarray(mesh.points, dtype=float) * float(scale)

    arr = np.asarray(mesh.point_data[scalar], dtype=float)
    if arr.ndim > 1:
        plot_name = f"|{scalar}|"
        mesh.point_data[plot_name] = np.linalg.norm(arr.reshape(len(arr), -1), axis=1)
    else:
        plot_name = scalar

    zmin, zmax = float(mesh.bounds[4]), float(mesh.bounds[5])
    return mesh, plot_name, zmin, zmax, float(scale)


def _scalar_values(mesh, plot_name):
    import numpy as np

    arr = np.asarray(mesh.point_data[plot_name], dtype=float).ravel()
    return arr[np.isfinite(arr)]


def _default_threshold(mesh, plot_name) -> float:
    """Starting Threshold: just above air so the slider is useful immediately."""
    import numpy as np

    arr = _scalar_values(mesh, plot_name)
    if arr.size == 0:
        return 0.0
    vmin = float(np.min(arr))
    vmax = float(np.max(arr))
    if not np.isfinite(vmin) or not np.isfinite(vmax) or vmax <= vmin:
        return vmin

    span = vmax - vmin
    air_cut = vmin + max(1e-8 * span, 1e-30)
    above = arr[arr > air_cut]
    if above.size == 0:
        return air_cut

    t = float(np.percentile(above, 2.0))
    soft_cap = vmin + 0.01 * span
    t = min(t, soft_cap)
    ceil = vmax - 1e-12 * max(abs(vmax), 1.0)
    return min(max(t, air_cut), ceil)


def _threshold_range(mesh, plot_name) -> tuple[float, float, float]:
    """Return (tmin, tmax, t0) for the Threshold slider."""
    arr = _scalar_values(mesh, plot_name)
    if arr.size == 0:
        return 0.0, 1.0, 0.0
    import numpy as np

    vmin = float(np.min(arr))
    vmax = float(np.max(arr))
    if vmax <= vmin:
        return vmin, vmin + 1.0, vmin
    return vmin, vmax, _default_threshold(mesh, plot_name)


def _apply_threshold(mesh, plot_name: str, thresh: float):
    """Keep cells/points at or above thresh; fall back to mesh if empty."""
    try:
        th = mesh.threshold(value=float(thresh), scalars=plot_name, preference="point")
        if getattr(th, "n_points", 0) > 0:
            return th
    except Exception:
        pass
    try:
        th = mesh.threshold(value=float(thresh), scalars=plot_name)
        if getattr(th, "n_points", 0) > 0:
            return th
    except Exception:
        pass
    return mesh


def _clim_of(mesh, plot_name: str):
    """Color limits from the *visible* mesh so turbo is not stuck on purple."""
    import numpy as np

    arr = _scalar_values(mesh, plot_name)
    if arr.size == 0:
        return None
    lo = float(np.min(arr))
    hi = float(np.max(arr))
    if not np.isfinite(lo) or not np.isfinite(hi) or hi <= lo:
        return None
    return [lo, hi]


def _structure_bounds(mesh, plot_name: str, thresh: float, *, tight_z: bool = True):
    """AABB of points above thresh (the layout blob).

    tight_z=True (default): use Z of those points only — required for camera focus,
    otherwise expanding Z to the full airbox column leaves the layout a speck in a
    tall empty frame.
    """
    import numpy as np

    arr = np.asarray(mesh.point_data[plot_name], dtype=float).ravel()
    pts = np.asarray(mesh.points, dtype=float)
    if arr.size != len(pts):
        return list(mesh.bounds)
    mask = np.isfinite(arr) & (arr >= float(thresh))
    if not np.any(mask):
        return list(mesh.bounds)
    hot = pts[mask]
    xmin, xmax = float(hot[:, 0].min()), float(hot[:, 0].max())
    ymin, ymax = float(hot[:, 1].min()), float(hot[:, 1].max())
    zmin, zmax = float(hot[:, 2].min()), float(hot[:, 2].max())
    if not tight_z:
        dx = max(0.02 * (xmax - xmin), 1e-9)
        dy = max(0.02 * (ymax - ymin), 1e-9)
        col = (
            (pts[:, 0] >= xmin - dx) & (pts[:, 0] <= xmax + dx)
            & (pts[:, 1] >= ymin - dy) & (pts[:, 1] <= ymax + dy)
        )
        if np.any(col):
            zmin = float(pts[col, 2].min())
            zmax = float(pts[col, 2].max())
    # Small pad so the layout is not clipped at the frame edge.
    pad_x = max(0.05 * (xmax - xmin), 1e-6)
    pad_y = max(0.05 * (ymax - ymin), 1e-6)
    pad_z = max(0.08 * (zmax - zmin), 1e-6)
    return [
        xmin - pad_x, xmax + pad_x,
        ymin - pad_y, ymax + pad_y,
        zmin - pad_z, zmax + pad_z,
    ]


def _bounds_span(bounds) -> float:
    return max(
        abs(bounds[1] - bounds[0]),
        abs(bounds[3] - bounds[2]),
        abs(bounds[5] - bounds[4]),
        1e-9,
    )


def _focus_camera(plotter, bounds, fill: float = 0.65) -> None:
    """Frame the given bounds so they occupy ~fill of the view."""
    import numpy as np

    b = [float(x) for x in bounds]
    cx = 0.5 * (b[0] + b[1])
    cy = 0.5 * (b[2] + b[3])
    cz = 0.5 * (b[4] + b[5])
    span = _bounds_span(b)
    try:
        # Prefer native bounds-aware reset when available.
        plotter.reset_camera(bounds=b)
    except TypeError:
        plotter.camera.focal_point = (cx, cy, cz)
        plotter.reset_camera()
    except Exception:
        plotter.camera.focal_point = (cx, cy, cz)
        try:
            plotter.reset_camera()
        except Exception:
            pass

    # Dolly so the hot region fills more of the viewport than a full-domain reset.
    try:
        cam = plotter.camera
        pos = np.asarray(cam.position, dtype=float)
        focal = np.asarray(cam.focal_point, dtype=float)
        direction = pos - focal
        dist = float(np.linalg.norm(direction))
        if dist > 1e-12 and span > 0:
            direction /= dist
            # Target distance: larger fill → closer camera.
            target = (span / max(fill, 0.05)) * 1.15
            cam.position = tuple(focal + direction * target)
            cam.focal_point = (cx, cy, cz)
    except Exception:
        pass
    try:
        plotter.render()
    except Exception:
        pass


def _pan_camera(plotter, sx: float, sy: float, span_holder: dict) -> None:
    """Translate camera in screen space (sx/sy = ±1 for one arrow step)."""
    import numpy as np

    scene_span = float(span_holder.get("span", 1.0))
    cam = plotter.camera
    position = np.asarray(cam.position, dtype=float)
    focal = np.asarray(cam.focal_point, dtype=float)
    view_up = np.asarray(cam.up, dtype=float)
    direction = focal - position
    right = np.cross(direction, view_up)
    rn = float(np.linalg.norm(right))
    if rn < 1e-12:
        return
    right /= rn
    up = np.cross(right, direction)
    un = float(np.linalg.norm(up))
    if un < 1e-12:
        return
    up /= un
    step = max(scene_span * 0.01, 1e-9)
    delta = right * (sx * step) + up * (sy * step)
    cam.position = tuple(position + delta)
    cam.focal_point = tuple(focal + delta)
    try:
        plotter.render()
    except Exception:
        pass


def _wire_navigation(plotter, span_holder: dict, focus_cb, full_reset_cb) -> None:
    """Arrows/WASD pan; F focuses hot region; R resets to full domain."""
    plotter.add_key_event("Left", lambda: _pan_camera(plotter, -1.0, 0.0, span_holder))
    plotter.add_key_event("Right", lambda: _pan_camera(plotter, 1.0, 0.0, span_holder))
    plotter.add_key_event("Up", lambda: _pan_camera(plotter, 0.0, 1.0, span_holder))
    plotter.add_key_event("Down", lambda: _pan_camera(plotter, 0.0, -1.0, span_holder))
    plotter.add_key_event("a", lambda: _pan_camera(plotter, -1.0, 0.0, span_holder))
    plotter.add_key_event("d", lambda: _pan_camera(plotter, 1.0, 0.0, span_holder))
    plotter.add_key_event("w", lambda: _pan_camera(plotter, 0.0, 1.0, span_holder))
    plotter.add_key_event("s", lambda: _pan_camera(plotter, 0.0, -1.0, span_holder))
    plotter.add_key_event("f", focus_cb)
    plotter.add_key_event("r", full_reset_cb)


def _set_windows_hwnd_icon(hwnd: int, icon_file: str) -> bool:
    """Set title-bar / taskbar icon on a Win32 HWND from a .ico (or image) file."""
    if sys.platform != "win32" or not hwnd or not icon_file or not os.path.isfile(icon_file):
        return False
    import ctypes

    user32 = ctypes.windll.user32
    IMAGE_ICON = 1
    LR_LOADFROMFILE = 0x00000010
    LR_DEFAULTSIZE = 0x00000040
    WM_SETICON = 0x0080
    ICON_SMALL = 0
    ICON_BIG = 1

    LoadImageW = user32.LoadImageW
    LoadImageW.argtypes = [
        ctypes.c_void_p,
        ctypes.c_wchar_p,
        ctypes.c_uint,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_uint,
    ]
    LoadImageW.restype = ctypes.c_void_p
    SendMessageW = user32.SendMessageW

    # Big + small variants (0,0 + LR_DEFAULTSIZE picks SM_CXICON / SM_CYICON).
    hbig = LoadImageW(None, icon_file, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE)
    hsmall = LoadImageW(None, icon_file, IMAGE_ICON, 16, 16, LR_LOADFROMFILE)
    if not hbig and not hsmall:
        return False
    if hsmall:
        SendMessageW(ctypes.c_void_p(hwnd), WM_SETICON, ICON_SMALL, hsmall)
    if hbig:
        SendMessageW(ctypes.c_void_p(hwnd), WM_SETICON, ICON_BIG, hbig)
    return True


def _plotter_hwnd(plotter) -> int:
    """Extract a Win32 HWND from VTK's GetGenericWindowId (may be a SWIG pointer string)."""
    import re

    ren = getattr(plotter, "ren_win", None) or getattr(plotter, "render_window", None)
    if ren is None:
        return 0
    for getter in ("GetGenericWindowId", "GetWindowId"):
        try:
            fn = getattr(ren, getter, None)
            if not fn:
                continue
            wid = fn()
            if wid is None:
                continue
            if isinstance(wid, int):
                return int(wid)
            s = str(wid)
            m = re.match(r"_([0-9a-fA-F]+)_p_void$", s)
            if m:
                return int(m.group(1), 16)
            # Plain hex / decimal
            try:
                return int(s, 0)
            except ValueError:
                pass
        except Exception:
            continue
    return 0


def _apply_qt_window_icon(plotter, icon_file: str) -> bool:
    """Set icon via Qt only when a QApplication already exists (safe)."""
    app = getattr(plotter, "app", None)
    win = getattr(plotter, "app_window", None)
    qapp = app
    if qapp is None:
        for mod in ("PySide6.QtWidgets", "PyQt5.QtWidgets"):
            try:
                QtWidgets = __import__(mod, fromlist=["QApplication"])
                qapp = QtWidgets.QApplication.instance()
                if qapp is not None:
                    break
            except Exception:
                continue
    if qapp is None and win is None:
        return False
    for mod in ("PySide6.QtGui", "PyQt5.QtGui"):
        try:
            QIcon = __import__(mod, fromlist=["QIcon"]).QIcon
            icon = QIcon(icon_file)
            if qapp is not None:
                qapp.setWindowIcon(icon)
            if win is not None:
                win.setWindowIcon(icon)
            return True
        except Exception as exc:
            print(f"field_volume_viewer: Qt icon skipped ({exc})", file=sys.stderr, flush=True)
            return False
    return False


def _wire_window_icon(plotter, icon_file: str | None) -> None:
    """Apply title-bar icon (Qt if available, else Win32 HWND for VTK Plotter)."""
    if not icon_file or not os.path.isfile(icon_file):
        return
    if _apply_qt_window_icon(plotter, icon_file):
        return
    if sys.platform != "win32":
        return

    state = {"done": False}

    def _try_apply(obj=None, event=None):
        if state["done"]:
            return
        hwnd = _plotter_hwnd(plotter)
        if not hwnd:
            return
        if _set_windows_hwnd_icon(hwnd, icon_file):
            state["done"] = True
            print(f"field_volume_viewer: set Win32 icon hwnd={hwnd}", flush=True)

    # Window may not exist yet — hook StartEvent / first render.
    try:
        iren = getattr(plotter, "iren", None)
        if iren is not None:
            iren.AddObserver("StartEvent", _try_apply)
    except Exception:
        pass
    try:
        ren = getattr(plotter, "ren_win", None)
        if ren is not None:
            ren.AddObserver("StartEvent", _try_apply)
            ren.AddObserver("ModifiedEvent", _try_apply)
    except Exception:
        pass
    _try_apply()


def _apply_branding(plotter, logo_path: str | None, icon_path: str | None = None) -> None:
    if logo_path:
        try:
            plotter.add_logo_widget(
                logo_path,
                position=(0.78, 0.88),
                size=(0.20, 0.10),
            )
        except Exception:
            try:
                plotter.add_logo_widget(logo_path)
            except Exception as exc:
                print(f"field_volume_viewer: logo widget skipped ({exc})", file=sys.stderr, flush=True)

    _wire_window_icon(plotter, icon_path or logo_path)


def _add_z_slider(plotter, callback, zmin: float, zmax: float, z0: float) -> None:
    """Z-clip slider — right half of bottom row. Default VTK style (readable fonts)."""
    plotter.add_slider_widget(
        callback,
        rng=[zmin, zmax],
        value=z0,
        title="Z (um)",
        pointa=(0.52, 0.08),
        pointb=(0.82, 0.08),
        title_height=0.025,
        slider_width=0.02,
        tube_width=0.005,
        fmt="%.4g",
    )


def _add_threshold_slider(plotter, callback, tmin: float, tmax: float, t0: float) -> None:
    """Threshold slider — left-center of bottom row. Default VTK style (readable fonts)."""
    plotter.add_slider_widget(
        callback,
        rng=[tmin, tmax],
        value=t0,
        title="Threshold",
        pointa=(0.18, 0.08),
        pointb=(0.48, 0.08),
        title_height=0.025,
        slider_width=0.02,
        tube_width=0.005,
        fmt="%.3g",
    )


def _mesh_add_kwargs(plot_name: str, log_scale: bool, clim=None, wireframe: bool = False) -> dict:
    """Surface of Z-clipped + thresholded field — metal/layout blocks, not tet soup."""
    kwargs = dict(
        scalars=plot_name,
        cmap="turbo",
        show_scalar_bar=True,
        scalar_bar_args={
            "title": plot_name,
            "n_labels": 3,
            "vertical": True,
            "position_x": 0.86,
            "position_y": 0.18,
            "width": 0.08,
            "height": 0.55,
            "fmt": "%.3g",
            "font_family": "arial",
            "title_font_size": 12,
            "label_font_size": 10,
            "color": "white",
        },
        ambient=0.55,
        diffuse=0.45,
        specular=0.05,
    )
    if wireframe:
        kwargs["style"] = "wireframe"
        kwargs["line_width"] = 1.2
        kwargs["opacity"] = 1.0
    else:
        kwargs["smooth_shading"] = True
        # Edges help read rectangular metal plates inside the volume.
        kwargs["show_edges"] = True
        kwargs["edge_color"] = "black"
        kwargs["line_width"] = 1
        kwargs["opacity"] = 0.55
    if clim is not None:
        kwargs["clim"] = clim
    if log_scale:
        kwargs["log_scale"] = True
    return kwargs


def _display_surface(mesh):
    """Outer surface of the thresholded region (= layout silhouette)."""
    try:
        surf = mesh.extract_surface()
        if getattr(surf, "n_points", 0) > 0:
            return surf
    except Exception:
        pass
    return mesh


def _add_domain_outline(plotter, mesh) -> None:
    try:
        plotter.add_mesh(
            mesh.outline(),
            color="lightgray",
            line_width=1,
            name="domain_outline",
        )
    except Exception as exc:
        print(f"field_volume_viewer: outline skipped ({exc})", file=sys.stderr, flush=True)


def _add_orientation_axes(plotter) -> None:
    """Corner tripod away from bottom sliders."""
    try:
        plotter.add_axes(viewport=(0.0, 0.78, 0.14, 0.98))
    except TypeError:
        try:
            plotter.add_axes()
        except Exception:
            pass
    except Exception:
        pass


def _add_nav_hint(plotter) -> None:
    try:
        plotter.add_text(
            "F focus layout · R full domain · E wire/surf · RMB box-zoom · Threshold / Z",
            position="upper_right",
            font_size=9,
            color="white",
            name="nav_hint",
        )
    except Exception:
        pass


def _enable_right_rubber_band_zoom(plotter) -> None:
    """Left = trackball rotate; right-drag = rubber-band zoom into the rectangle."""
    iren = getattr(plotter, "iren", None)
    if iren is None:
        return
    try:
        from pyvista import _vtk
        from pyvista.plotting.render_window_interactor import InteractorStyleCaptureMixin
    except Exception as exc:
        print(f"field_volume_viewer: rubber-band zoom unavailable ({exc})", file=sys.stderr, flush=True)
        return

    class _TrackballRightRubberZoom(
        InteractorStyleCaptureMixin, _vtk.vtkInteractorStyleTrackballCamera
    ):
        def __init__(self, parent):
            InteractorStyleCaptureMixin.__init__(self, parent)
            self._rbz = _vtk.vtkInteractorStyleRubberBandZoom()
            self._rb_active = False

        def SetInteractor(self, interactor):  # noqa: N802 — VTK API
            _vtk.vtkInteractorStyleTrackballCamera.SetInteractor(self, interactor)
            try:
                self._rbz.SetInteractor(interactor)
            except Exception:
                pass

        def OnRightButtonDown(self):
            inter = self.GetInteractor()
            if inter is None:
                return
            x, y = inter.GetEventPosition()
            self.FindPokedRenderer(x, y)
            ren = self.GetCurrentRenderer()
            if ren is None:
                return
            try:
                self._rbz.SetInteractor(inter)
                self._rbz.SetCurrentRenderer(ren)
                self._rb_active = True
                self._rbz.OnLeftButtonDown()
            except Exception:
                self._rb_active = False
                # Fallback: keep default dolly if rubber-band fails.
                try:
                    super().OnRightButtonDown()
                except Exception:
                    pass

        def OnRightButtonUp(self):
            if self._rb_active:
                try:
                    self._rbz.OnLeftButtonUp()
                except Exception:
                    pass
                self._rb_active = False
                try:
                    self.GetInteractor().Render()
                except Exception:
                    pass
                return
            try:
                super().OnRightButtonUp()
            except Exception:
                pass

        def OnMouseMove(self):
            if self._rb_active:
                try:
                    self._rbz.OnMouseMove()
                except Exception:
                    pass
                return
            super().OnMouseMove()

    try:
        iren.style = _TrackballRightRubberZoom(iren)
        print("field_volume_viewer: right-button rubber-band zoom enabled", flush=True)
    except Exception as exc:
        print(f"field_volume_viewer: rubber-band zoom setup failed ({exc})", file=sys.stderr, flush=True)


def _apply_dark_chrome(plotter, color: str = "#2a2a2a") -> None:
    """VTK scene + Qt window chrome → near dark-gray (not white)."""
    try:
        plotter.set_background(color)
    except Exception:
        pass
    win = getattr(plotter, "app_window", None)
    if win is not None:
        try:
            win.setStyleSheet(
                f"QMainWindow, QWidget {{ background-color: {color}; color: #ddd; }}"
            )
        except Exception:
            pass
    app = getattr(plotter, "app", None)
    if app is not None:
        try:
            app.setStyleSheet(
                f"QMainWindow, QDialog, QWidget {{ background-color: {color}; }}"
            )
        except Exception:
            pass


def _setup_interactive(plotter, mesh, plot_name: str, zmin, zmax, z0, log_scale: bool,
                       logo_path, icon_path, use_qt_branding: bool) -> None:
    """Show metal/layout shapes inside the volume via Z-clip + threshold surface.

    Layout = rectangular metal blocks inside the volume. Z clip starts at the
    *top* of that layout so upper layers are not chopped (mid-plane from 2D Field
    was cutting the roof off).
    """
    tmin, tmax, t0 = _threshold_range(mesh, plot_name)
    z_full = float(zmax)
    domain_bounds = list(mesh.bounds)

    hot0 = _structure_bounds(mesh, plot_name, t0, tight_z=True)
    # Top of layout metals (+ pad already in hot0[5]). Never start below this.
    z_layout_top = float(hot0[5])
    if z0 is not None:
        z_start = max(float(z0), z_layout_top)
    else:
        z_start = z_layout_top
    z_start = min(max(z_start, float(zmin)), float(zmax))

    state = {
        "z": z_start,
        "thresh": float(t0),
        "actor": None,
        "wire": False,
    }
    span_holder = {"span": _bounds_span(hot0)}

    def _z_clipped():
        if state["z"] >= z_full - 1e-9 * max(abs(z_full - zmin), 1.0):
            return mesh
        origin = (
            0.5 * (mesh.bounds[0] + mesh.bounds[1]),
            0.5 * (mesh.bounds[2] + mesh.bounds[3]),
            float(state["z"]),
        )
        return mesh.clip(normal=(0.0, 0.0, 1.0), origin=origin, inplace=False)

    def _refresh_actor():
        if state["actor"] is not None:
            try:
                plotter.remove_actor(state["actor"])
            except Exception:
                pass
            state["actor"] = None

        th = _apply_threshold(_z_clipped(), plot_name, state["thresh"])
        geom = _display_surface(th)
        if getattr(geom, "n_points", 0) <= 0:
            return
        clim = _clim_of(th, plot_name)
        kwargs = _mesh_add_kwargs(plot_name, log_scale, clim=clim, wireframe=state["wire"])
        state["actor"] = plotter.add_mesh(geom, **kwargs)
        try:
            plotter.render()
        except Exception:
            pass

    def _update_pan_span():
        span_holder["span"] = _bounds_span(
            _structure_bounds(mesh, plot_name, state["thresh"], tight_z=True)
        )

    def _focus_layout():
        """Zoom on layout shapes; keep Z at/above layout top (no mid-plane chop)."""
        # Ensure upper metals are included.
        state["z"] = max(float(state["z"]), z_layout_top)
        state["z"] = min(state["z"], z_full)
        _refresh_actor()
        hb = _structure_bounds(mesh, plot_name, state["thresh"], tight_z=True)
        span_holder["span"] = _bounds_span(hb)
        _focus_camera(plotter, hb, fill=0.82)
        print(
            f"field_volume_viewer: focus layout z={state['z']:.4g} "
            f"(layout_top={z_layout_top:.4g}) "
            f"XY={hb[1]-hb[0]:.4g}x{hb[3]-hb[2]:.4g}",
            flush=True,
        )

    def _frame_domain():
        span_holder["span"] = _bounds_span(domain_bounds)
        _focus_camera(plotter, domain_bounds, fill=0.88)

    def _toggle_wire():
        state["wire"] = not state["wire"]
        _refresh_actor()

    def _on_z(value):
        state["z"] = float(value)
        _refresh_actor()

    def _on_thresh(value):
        state["thresh"] = float(value)
        _update_pan_span()
        _refresh_actor()

    _apply_dark_chrome(plotter)
    _add_domain_outline(plotter, mesh)
    _refresh_actor()
    _add_orientation_axes(plotter)
    _enable_right_rubber_band_zoom(plotter)
    _wire_navigation(plotter, span_holder, _focus_layout, _frame_domain)
    plotter.add_key_event("e", _toggle_wire)
    _add_nav_hint(plotter)
    if use_qt_branding:
        _apply_branding(plotter, logo_path, icon_path)
    elif logo_path:
        try:
            plotter.add_logo_widget(logo_path, position=(0.78, 0.88), size=(0.20, 0.10))
        except Exception:
            try:
                plotter.add_logo_widget(logo_path)
            except Exception as exc:
                print(f"field_volume_viewer: logo widget skipped ({exc})", file=sys.stderr, flush=True)

    _add_threshold_slider(plotter, _on_thresh, tmin, tmax, t0)
    _add_z_slider(plotter, _on_z, zmin, zmax, state["z"])
    _focus_layout()

    print(
        f"field_volume_viewer: threshold={t0:.4g} z={state['z']:.4g} "
        f"layout_top={z_layout_top:.4g} zmax={z_full:.4g}",
        flush=True,
    )


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="EMStudio interactive Field 3D viewer")
    p.add_argument("--input", "-i", required=True, help="Field dump (.pvd/.pvtu/.vtu/.vtk)")
    p.add_argument("--z-um", type=float, default=None, help="Initial Z clip [µm]")
    p.add_argument("--log", action="store_true", help="Log10 color scale")
    p.add_argument("--quantity", default=None, help="Preferred scalar name")
    p.add_argument("--scale-to-um", type=float, default=None)
    p.add_argument("--logo", default=None, help="Path to EMStudio logo PNG")
    p.add_argument("--icon", default=None, help="Path to window icon (.ico preferred)")
    args = p.parse_args(argv)

    logo_path = _find_logo(args.logo)
    icon_path = _find_icon(args.icon, logo_path)

    pv = _try_import_pyvista()

    mesh, plot_name, zmin, zmax, _scale = _load_mesh(
        pv, args.input, args.scale_to_um, args.quantity
    )
    # None → viewer picks Z at the layout (hot-region) center, not zmax.
    z0 = None if args.z_um is None else min(max(float(args.z_um), zmin), zmax)

    title = f"EMStudio Field 3D — {os.path.basename(args.input)}"

    # Prefer pyvistaqt if available (closer to Volker); else blocking Plotter.show().
    try:
        from pyvistaqt import BackgroundPlotter

        plotter = BackgroundPlotter(title=title, auto_update=True)
        _apply_dark_chrome(plotter)
        _setup_interactive(
            plotter, mesh, plot_name, zmin, zmax, z0, args.log,
            logo_path, icon_path, use_qt_branding=True,
        )
        print(
            f"field_volume_viewer: opened BackgroundPlotter for {args.input} "
            f"(bounds um: X={mesh.bounds[1]-mesh.bounds[0]:.4g} "
            f"Y={mesh.bounds[3]-mesh.bounds[2]:.4g} "
            f"Z={mesh.bounds[5]-mesh.bounds[4]:.4g})",
            flush=True,
        )
        import time
        while plotter.app_window.isVisible():
            plotter.app.processEvents()
            time.sleep(0.03)
        return 0
    except Exception as exc:
        print(f"field_volume_viewer: pyvistaqt unavailable ({exc}); using Plotter.show()",
              file=sys.stderr, flush=True)

    pl = pv.Plotter(title=title)
    _apply_dark_chrome(pl)
    _setup_interactive(
        pl, mesh, plot_name, zmin, zmax, z0, args.log,
        logo_path, icon_path, use_qt_branding=False,
    )
    icon_file = icon_path or logo_path
    if icon_file:
        state = {"done": False}

        def _on_render(obj=None, event=None):
            if state["done"]:
                return
            hwnd = _plotter_hwnd(pl)
            if not hwnd:
                return
            if _set_windows_hwnd_icon(hwnd, icon_file):
                state["done"] = True
                print(f"field_volume_viewer: set Win32 icon hwnd={hwnd}", flush=True)
                try:
                    pl.ren_win.RemoveObservers("RenderEvent")
                except Exception:
                    pass

        try:
            pl.ren_win.AddObserver("RenderEvent", _on_render)
        except Exception as exc:
            print(f"field_volume_viewer: icon observer skipped ({exc})", file=sys.stderr, flush=True)
    print(
        f"field_volume_viewer: showing Plotter for {args.input} "
        f"(bounds um: X={mesh.bounds[1]-mesh.bounds[0]:.4g} "
        f"Y={mesh.bounds[3]-mesh.bounds[2]:.4g} "
        f"Z={mesh.bounds[5]-mesh.bounds[4]:.4g})",
        flush=True,
    )
    pl.show()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as exc:
        print(f"field_volume_viewer: fatal: {exc}", file=sys.stderr, flush=True)
        raise SystemExit(1) from exc
