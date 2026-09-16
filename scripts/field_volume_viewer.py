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


def _pan_camera(plotter, sx: float, sy: float, scene_span: float) -> None:
    """Translate camera in screen space (sx/sy = ±1 for one arrow step)."""
    import numpy as np

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


def _wire_arrow_pan(plotter, scene_span: float) -> None:
    """Arrow keys (and WASD) pan the view; F resets the camera."""
    plotter.add_key_event("Left", lambda: _pan_camera(plotter, -1.0, 0.0, scene_span))
    plotter.add_key_event("Right", lambda: _pan_camera(plotter, 1.0, 0.0, scene_span))
    plotter.add_key_event("Up", lambda: _pan_camera(plotter, 0.0, 1.0, scene_span))
    plotter.add_key_event("Down", lambda: _pan_camera(plotter, 0.0, -1.0, scene_span))
    plotter.add_key_event("a", lambda: _pan_camera(plotter, -1.0, 0.0, scene_span))
    plotter.add_key_event("d", lambda: _pan_camera(plotter, 1.0, 0.0, scene_span))
    plotter.add_key_event("w", lambda: _pan_camera(plotter, 0.0, 1.0, scene_span))
    plotter.add_key_event("s", lambda: _pan_camera(plotter, 0.0, -1.0, scene_span))
    plotter.add_key_event("f", plotter.reset_camera)


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
    """Z-clip slider with enough bottom margin so the title is not clipped."""
    plotter.add_slider_widget(
        callback,
        rng=[zmin, zmax],
        value=z0,
        title="Z (um)",
        pointa=(0.20, 0.18),
        pointb=(0.58, 0.18),
        style="modern",
        title_height=0.035,
        fmt="%.4g",
    )


def _mesh_add_kwargs(plot_name: str, log_scale: bool) -> dict:
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
        },
        smooth_shading=True,
        ambient=0.45,
        diffuse=0.5,
    )
    if log_scale:
        kwargs["log_scale"] = True
    return kwargs


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
    z0 = args.z_um if args.z_um is not None else 0.5 * (zmin + zmax)
    z0 = min(max(float(z0), zmin), zmax)

    title = f"EMStudio Field 3D — {os.path.basename(args.input)}"

    def _clipped(z_clip: float):
        origin = (0.5 * (mesh.bounds[0] + mesh.bounds[1]),
                  0.5 * (mesh.bounds[2] + mesh.bounds[3]),
                  float(z_clip))
        return mesh.clip(normal=(0.0, 0.0, 1.0), origin=origin, inplace=False)

    add_kwargs = _mesh_add_kwargs(plot_name, args.log)

    span = max(
        abs(mesh.bounds[1] - mesh.bounds[0]),
        abs(mesh.bounds[3] - mesh.bounds[2]),
        abs(mesh.bounds[5] - mesh.bounds[4]),
        1e-9,
    )

    # Prefer pyvistaqt if available (closer to Volker); else blocking Plotter.show().
    try:
        from pyvistaqt import BackgroundPlotter

        plotter = BackgroundPlotter(title=title, auto_update=True)
        plotter.set_background("white")
        actor = plotter.add_mesh(_clipped(z0), **add_kwargs)
        plotter.add_axes()
        _wire_arrow_pan(plotter, span)
        _apply_branding(plotter, logo_path, icon_path)

        def _on_z(value):
            nonlocal actor
            plotter.remove_actor(actor)
            actor = plotter.add_mesh(_clipped(float(value)), **add_kwargs)
            plotter.render()

        _add_z_slider(plotter, _on_z, zmin, zmax, z0)
        plotter.reset_camera()
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
    pl.set_background("white")
    actor_box = {"actor": None}

    def _on_z(value):
        if actor_box["actor"] is not None:
            pl.remove_actor(actor_box["actor"])
        actor_box["actor"] = pl.add_mesh(_clipped(float(value)), **add_kwargs)

    _on_z(z0)
    pl.add_axes()
    _wire_arrow_pan(pl, span)
    # Logo now; window icon after the HWND exists (see RenderEvent hook).
    if logo_path:
        try:
            pl.add_logo_widget(logo_path, position=(0.78, 0.88), size=(0.20, 0.10))
        except Exception:
            try:
                pl.add_logo_widget(logo_path)
            except Exception as exc:
                print(f"field_volume_viewer: logo widget skipped ({exc})", file=sys.stderr, flush=True)
    _add_z_slider(pl, _on_z, zmin, zmax, z0)
    pl.reset_camera()
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
