#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Export a Z-clip field heatmap (+ optional in-plane arrows) for EMStudio Layout Field view.

Reads Palace .pvd / OpenEMS VTK / Elmer .vtu via PyVista, writes:
  <outdir>/field_slice_meta.json
  <outdir>/field_slice.png

Coordinates in meta are micrometres, GDS-style Y-up (same as LayoutView polygons).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from typing import Optional


def _die(msg: str, code: int = 1) -> None:
    print(f"field_slice_export: {msg}", file=sys.stderr)
    sys.exit(code)


def _try_import_pyvista():
    try:
        import pyvista as pv  # noqa: F401
        return pv
    except Exception as exc:  # pragma: no cover
        _die(
            "PyVista and Pillow are required for Field view. Install with:\n"
            "  pip install pyvista pillow\n"
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
    # unique, preserve order
    seen = set()
    out = []
    for n in names:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def _pick_scalar(names, prefer: Optional[str]):
    if prefer:
        for n in names:
            if n.lower() == prefer.lower():
                return n
        for n in names:
            if prefer.lower() in n.lower():
                return n

    # EM magnitude-like
    for key in ("|E|", "E_abs", "Eabs", "magE", "AbsE", "normE"):
        for n in names:
            if n.lower() == key.lower():
                return n
    for n in names:
        nl = n.lower()
        if "e" in nl and ("abs" in nl or "mag" in nl or "norm" in nl):
            return n

    # Temperature (Elmer)
    for key in ("temperature", "temp", "t"):
        for n in names:
            if n.lower() == key:
                return n
    for n in names:
        if "temp" in n.lower():
            return n

    # First non-vector-ish scalar name
    for n in names:
        nl = n.lower()
        if nl.endswith("_x") or nl.endswith("_y") or nl.endswith("_z"):
            continue
        if nl in ("e", "h", "b", "d", "j", "s"):
            continue
        return n
    return names[0] if names else None


def _pick_vector(names: list[str]) -> str | None:
    for key in ("S", "Poynting", "S_abs", "H", "E"):
        for n in names:
            if n.lower() == key.lower():
                return n
    for n in names:
        nl = n.lower()
        if "poynt" in nl or nl == "s" or nl.startswith("s_"):
            return n
    return None


def _mesh_bounds_um(mesh, scale_to_um: float):
    b = mesh.bounds  # xmin,xmax,ymin,ymax,zmin,zmax in mesh units
    return [v * scale_to_um for v in b]


def _guess_scale_to_um(bounds) -> float:
    """Palace dumps are often metres; OpenEMS/Elmer may already be µm or m."""
    span = max(abs(bounds[1] - bounds[0]), abs(bounds[3] - bounds[2]), abs(bounds[5] - bounds[4]))
    if span <= 0:
        return 1.0e6  # assume metres → µm
    if span < 1e-2:
        return 1.0e6  # metres → µm
    if span < 50:
        return 1.0  # already µm-ish (chip scale)
    if span > 1e4:
        return 1.0e-3  # nm → µm
    return 1.0


def _colormap_rgba(values, log_scale: bool, vmin=None, vmax=None):
    import numpy as np

    v = np.asarray(values, dtype=float).ravel()
    v = np.nan_to_num(v, nan=0.0, posinf=0.0, neginf=0.0)
    if vmin is None or vmax is None:
        if log_scale:
            ref = np.log10(np.maximum(v, 1e-30))
        else:
            ref = v
        if vmin is None:
            vmin = float(np.percentile(ref, 2)) if ref.size else 0.0
        if vmax is None:
            vmax = float(np.percentile(ref, 98)) if ref.size else 1.0
    if log_scale:
        v = np.log10(np.maximum(v, 1e-30))
    if not math.isfinite(vmin) or not math.isfinite(vmax) or abs(vmax - vmin) < 1e-30:
        vmin, vmax = 0.0, 1.0
    t = np.clip((v - vmin) / (vmax - vmin), 0.0, 1.0)

    # Simple turbo-ish LUT (blue → cyan → yellow → red)
    r = np.clip(1.5 - np.abs(3.5 * t - 2.5), 0, 1)
    g = np.clip(1.5 - np.abs(3.5 * t - 1.5), 0, 1)
    b = np.clip(1.5 - np.abs(3.5 * t - 0.5), 0, 1)
    a = np.full_like(t, 0.85)
    rgba = np.stack([r, g, b, a], axis=-1)
    rgba_u8 = (rgba * 255.0).astype(np.uint8)
    return rgba_u8, vmin, vmax


def _auto_z_um(data_mesh, scalar_name: str, scale: float, zmin: float, zmax: float,
               xmin: float, xmax: float, ymin: float, ymax: float) -> float:
    """Pick Z with strongest field (max T / |E|) inside the XY ROI if possible."""
    import numpy as np

    pts = np.asarray(data_mesh.points, dtype=float) * scale
    arr = np.asarray(data_mesh.point_data[scalar_name], dtype=float)
    if arr.ndim > 1:
        mag = np.linalg.norm(arr.reshape(len(arr), -1), axis=1)
    else:
        mag = arr.ravel()
    if mag.size == 0:
        return 0.5 * (zmin + zmax)

    # Prefer points inside current layout ROI (slightly padded).
    pad_x = max((xmax - xmin) * 0.2, 2.0)
    pad_y = max((ymax - ymin) * 0.2, 2.0)
    in_roi = (
        (pts[:, 0] >= xmin - pad_x)
        & (pts[:, 0] <= xmax + pad_x)
        & (pts[:, 1] >= ymin - pad_y)
        & (pts[:, 1] <= ymax + pad_y)
    )
    if in_roi.sum() >= 8:
        idx = int(np.argmax(mag[in_roi]))
        z = float(pts[in_roi][idx, 2])
    else:
        idx = int(np.argmax(mag))
        z = float(pts[idx, 2])
    return min(max(z, zmin), zmax)


def _field_active_xy_um(data_mesh, scalar_name: str, scale: float, z_um: float,
                        mxmin, mxmax, mymin, mymax):
    """XY bbox of the 'interesting' field near z (hot / high |E|), not the full air-box."""
    import numpy as np

    pts = np.asarray(data_mesh.points, dtype=float) * scale
    arr = np.asarray(data_mesh.point_data[scalar_name], dtype=float)
    if arr.ndim > 1:
        mag = np.linalg.norm(arr.reshape(len(arr), -1), axis=1)
    else:
        mag = np.asarray(arr, dtype=float).ravel()

    # Band around clip Z (~2% of stack, at least 2 µm).
    dz = max(2.0, 0.02 * max(mymax - mymin, mxmax - mxmin, 1.0))
    # Prefer absolute z thickness from mesh
    zspan = max(abs(pts[:, 2].max() - pts[:, 2].min()) if pts.size else 1.0, 1.0)
    dz = max(2.0, 0.03 * zspan)

    near = np.abs(pts[:, 2] - z_um) <= dz
    if near.sum() < 16:
        near = np.abs(pts[:, 2] - z_um) <= max(dz * 3, 10.0)
    if near.sum() == 0:
        return None

    vals = mag[near]
    vlo = float(np.percentile(vals, 5))
    vhi = float(np.percentile(vals, 95))
    span = max(vhi - vlo, 1e-30)
    # Keep the elevated / active region (above ~15% of local dynamic range).
    thr = vlo + 0.15 * span
    active = near & (mag >= thr)
    if active.sum() < 8:
        active = near & (mag >= vlo + 0.05 * span)
    if active.sum() < 4:
        return None

    xs = pts[active, 0]
    ys = pts[active, 1]
    return float(xs.min()), float(xs.max()), float(ys.min()), float(ys.max())


def _union_and_frame(xmin, xmax, ymin, ymax, mxmin, mxmax, mymin, mymax,
                     layout=None, pad_frac=0.12, max_aspect=2.5):
    """Union optional layout bbox, pad, and expand short axis so the frame is not a thin strip."""
    if layout is not None:
        lx0, lx1, ly0, ly1 = layout
        xmin = min(xmin, lx0)
        xmax = max(xmax, lx1)
        ymin = min(ymin, ly0)
        ymax = max(ymax, ly1)

    w = max(xmax - xmin, 1e-9)
    h = max(ymax - ymin, 1e-9)
    pad_x = max(w * pad_frac, 2.0)
    pad_y = max(h * pad_frac, 2.0)
    xmin -= pad_x
    xmax += pad_x
    ymin -= pad_y
    ymax += pad_y
    w = xmax - xmin
    h = ymax - ymin

    # Expand the shorter side toward a pleasant aspect (layout stays centered).
    if w / h > max_aspect:
        target_h = w / max_aspect
        extra = 0.5 * (target_h - h)
        ymin -= extra
        ymax += extra
    elif h / w > max_aspect:
        target_w = h / max_aspect
        extra = 0.5 * (target_w - w)
        xmin -= extra
        xmax += extra

    xmin = max(mxmin, xmin)
    xmax = min(mxmax, xmax)
    ymin = max(mymin, ymin)
    ymax = min(mymax, ymax)
    if xmax <= xmin or ymax <= ymin:
        return mxmin, mxmax, mymin, mymax
    return xmin, xmax, ymin, ymax


def _volume_cache_path(outdir: str) -> str:
    return os.path.join(outdir, "field_volume_cache.npz")


def _cache_fingerprint(mesh_path: str, layout_roi, resolution: int) -> str:
    st = os.stat(mesh_path)
    parts = [os.path.abspath(mesh_path), str(getattr(st, "st_mtime_ns", int(st.st_mtime * 1e9))), str(int(resolution))]
    if layout_roi is not None:
        parts.extend(f"{v:.6g}" for v in layout_roi)
    else:
        parts.append("nolayout")
    return "|".join(parts)


def _atomic_save_png(rgba, png_path: str) -> None:
    """Write PNG via temp file + replace to avoid torn reads in the GUI."""
    from PIL import Image
    import tempfile

    folder = os.path.dirname(png_path) or "."
    fd, tmp = tempfile.mkstemp(prefix="field_slice_", suffix=".png", dir=folder)
    os.close(fd)
    try:
        Image.fromarray(rgba, mode="RGBA").save(tmp, format="PNG")
        os.replace(tmp, png_path)
    except Exception:
        try:
            if os.path.isfile(tmp):
                os.remove(tmp)
        except OSError:
            pass
        raise


def _atomic_save_json(meta: dict, meta_path: str) -> None:
    import tempfile

    folder = os.path.dirname(meta_path) or "."
    fd, tmp = tempfile.mkstemp(prefix="field_meta_", suffix=".json", dir=folder)
    os.close(fd)
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=2)
        os.replace(tmp, meta_path)
    except Exception:
        try:
            if os.path.isfile(tmp):
                os.remove(tmp)
        except OSError:
            pass
        raise


def _write_slice_outputs(
    outdir,
    img_vals,
    log_scale,
    global_vmin,
    global_vmax,
    z_clip,
    zmin,
    zmax,
    xmin,
    xmax,
    ymin,
    ymax,
    scalar_name,
    scale,
    arrows,
    arrow_list,
    names,
    mesh_path,
    status="",
):
    import numpy as np

    ny, nx = img_vals.shape
    local = img_vals.ravel()
    local_plot = np.log10(np.maximum(local, 1e-30)) if log_scale else local
    local_span = float(np.percentile(local_plot, 98) - np.percentile(local_plot, 2)) if local_plot.size else 0.0
    global_span = (global_vmax - global_vmin) if (global_vmin is not None and global_vmax is not None) else 0.0
    if global_span > 1e-30 and local_span < 0.15 * global_span:
        rgba, vmin, vmax = _colormap_rgba(img_vals, log_scale, None, None)
    else:
        rgba, vmin, vmax = _colormap_rgba(img_vals, log_scale, global_vmin, global_vmax)
    rgba = rgba.reshape(ny, nx, 4)
    png_path = os.path.join(outdir, "field_slice.png")
    _atomic_save_png(rgba, png_path)

    meta = {
        "version": 1,
        "quantity": scalar_name,
        "z_um": z_clip,
        "zmin_um": zmin,
        "zmax_um": zmax,
        "xmin_um": xmin,
        "xmax_um": xmax,
        "ymin_um": ymin,
        "ymax_um": ymax,
        "png": "field_slice.png",
        "log_scale": bool(log_scale),
        "show_arrows": bool(arrows),
        "vmin": vmin,
        "vmax": vmax,
        "scale_to_um": scale,
        "arrows": arrow_list or [],
        "status": status,
        "arrays": names or [],
        "source": os.path.abspath(mesh_path),
        "from_volume_cache": True,
    }
    meta_path = os.path.join(outdir, "field_slice_meta.json")
    _atomic_save_json(meta, meta_path)
    print(json.dumps({"ok": True, "meta": meta_path, "png": png_path, "cache": True}))
    return meta


def _plane_from_volume(volume, zs, z_clip):
    import numpy as np

    if z_clip <= zs[0]:
        return volume[0]
    if z_clip >= zs[-1]:
        return volume[-1]
    i = int(np.searchsorted(zs, z_clip) - 1)
    i = max(0, min(i, len(zs) - 2))
    t = (z_clip - zs[i]) / max(zs[i + 1] - zs[i], 1e-30)
    return (1.0 - t) * volume[i] + t * volume[i + 1]


def _try_export_from_volume_cache(
    outdir,
    mesh_path,
    z_um,
    log_scale,
    arrows,
    layout_roi,
    resolution,
):
    """Volume-cache fast path disabled: interpolated Z layers looked torn/blocky.

    Kept as a stub so older call sites stay valid; always falls through to a
    direct probe of the requested Z plane (with vtkValidPointMask).
    """
    del mesh_path, z_um, log_scale, arrows, layout_roi, resolution
    # Drop stale caches so they are not reused by older script copies.
    path = _volume_cache_path(outdir)
    if os.path.isfile(path):
        try:
            os.remove(path)
        except OSError:
            pass
    return None


def _build_volume_cache(pv, data_mesh, scalar_name, scale, xs, ys, zs, outdir, fingerprint,
                        zmin, zmax, global_vmin, global_vmax, names):
    """Sample a coarse 3D grid once so later Z moves are cheap."""
    import numpy as np

    ny, nx = len(ys), len(xs)
    layers = []
    for z in zs:
        xx2, yy2 = np.meshgrid(xs / scale, ys / scale, indexing="xy")
        zz2 = np.full_like(xx2, z / scale)
        pts2 = np.column_stack([xx2.ravel(), yy2.ravel(), zz2.ravel()])
        s2 = pv.PolyData(pts2).sample(data_mesh)
        a2 = np.asarray(s2.point_data[scalar_name], dtype=float).ravel()
        if a2.size != ny * nx:
            a2 = np.linalg.norm(np.asarray(s2.point_data[scalar_name], dtype=float).reshape(-1, 3), axis=1)
        layers.append(a2.reshape(ny, nx))
    volume = np.stack(layers, axis=0)

    np.savez_compressed(
        _volume_cache_path(outdir),
        fingerprint=np.asarray(fingerprint),
        volume=volume.astype(np.float32),
        xs=xs.astype(np.float64),
        ys=ys.astype(np.float64),
        zs=zs.astype(np.float64),
        zmin=np.float64(zmin),
        zmax=np.float64(zmax),
        scale=np.float64(scale),
        scalar_name=np.asarray(scalar_name),
        global_vmin=np.float64(global_vmin if global_vmin is not None else 0.0),
        global_vmax=np.float64(global_vmax if global_vmax is not None else 1.0),
        arrays=np.asarray(list(names), dtype=str),
    )
    return volume


def _as_magnitude(arr) -> "np.ndarray":
    import numpy as np

    a = np.asarray(arr, dtype=float)
    if a.ndim == 1:
        return a.ravel()
    return np.linalg.norm(a.reshape(a.shape[0], -1), axis=1)


def _smooth_grid(grid, sigma: float = 2.75):
    """Blur sampled values so coarse tet facets don't dominate the PNG."""
    import numpy as np

    out = np.array(grid, dtype=float, copy=True)
    finite = np.isfinite(out)
    if not finite.any():
        return out
    fill = float(np.nanmedian(out[finite]))
    filled = np.where(finite, out, fill)
    try:
        from scipy.ndimage import gaussian_filter

        blurred = gaussian_filter(filled, sigma=sigma, mode="nearest")
    except Exception:
        # Separable box blur fallback (~sigma*2.5 taps)
        n = max(3, int(round(sigma * 2.5)) | 1)
        k = np.ones(n, dtype=float) / n
        pad = n // 2
        tmp = np.pad(filled, ((0, 0), (pad, pad)), mode="edge")
        row = np.vstack([np.convolve(tmp[i], k, mode="valid") for i in range(tmp.shape[0])])
        tmp2 = np.pad(row, ((pad, pad), (0, 0)), mode="edge")
        blurred = np.column_stack(
            [np.convolve(tmp2[:, j], k, mode="valid") for j in range(tmp2.shape[1])]
        )
    out = blurred
    out[~finite] = np.nan
    return out


def _sample_z_grid(pv, data_mesh, scalar_name: str, scale: float, xs, ys, z_um: float):
    """Sample scalar (or vector magnitude) on a regular XY grid at z_um.

    Prefer a thin Z-band of mesh points + griddata (smooth), fall back to
    ImageData probe. Always apply a light blur afterward.
    """
    import numpy as np

    ny = len(ys)
    nx = len(xs)
    z_native = z_um / scale
    xx, yy = np.meshgrid(xs / scale, ys / scale, indexing="xy")
    grid = None
    mask = None

    # 1) Band sample + griddata (avoids huge tet facets on the clip plane)
    try:
        pts = np.asarray(data_mesh.points, dtype=float)
        arr = _as_magnitude(data_mesh.point_data[scalar_name])
        zspan = float(np.ptp(pts[:, 2])) if pts.size else 1.0
        dz = max(0.75 / scale, 0.01 * zspan)
        near = np.abs(pts[:, 2] - z_native) <= dz
        if near.sum() < 24:
            near = np.abs(pts[:, 2] - z_native) <= max(dz * 3.0, 2.0 / scale)
        if near.sum() >= 16:
            try:
                from scipy.interpolate import griddata

                g = griddata(
                    pts[near, :2],
                    arr[near],
                    (xx, yy),
                    method="linear",
                    fill_value=np.nan,
                )
                if np.isfinite(g).mean() < 0.35:
                    g = griddata(
                        pts[near, :2],
                        arr[near],
                        (xx, yy),
                        method="nearest",
                    )
                grid = np.asarray(g, dtype=float)
                mask = np.isfinite(grid).astype(np.uint8)
            except Exception:
                grid = None
    except Exception:
        grid = None

    # 2) ImageData / PolyData probe fallback
    if grid is None:
        try:
            dx = (float(xs[-1]) - float(xs[0])) / max(nx - 1, 1) / scale
            dy = (float(ys[-1]) - float(ys[0])) / max(ny - 1, 1) / scale
            grid_img = pv.ImageData()
            grid_img.origin = (float(xs[0]) / scale, float(ys[0]) / scale, z_native)
            grid_img.spacing = (dx, dy, max(abs(dx), abs(dy), 1e-9))
            grid_img.dimensions = (nx, ny, 1)
            sampled = grid_img.sample(data_mesh)
            mag = _as_magnitude(sampled.point_data[scalar_name])
            if mag.size != nx * ny:
                raise RuntimeError(f"sample size mismatch: got {mag.size}, expected {nx * ny}")
            grid = mag.reshape(nx, ny).T.astype(float)
            if "vtkValidPointMask" in sampled.point_data:
                mask = np.asarray(sampled.point_data["vtkValidPointMask"]).ravel().reshape(nx, ny).T
                grid = grid.copy()
                grid[mask == 0] = np.nan
        except Exception:
            pts2 = np.column_stack([xx.ravel(), yy.ravel(), np.full(xx.size, z_native)])
            sampled = pv.PolyData(pts2).sample(data_mesh)
            if scalar_name not in sampled.point_data:
                raise RuntimeError(f"array {scalar_name!r} missing after sample")
            mag = _as_magnitude(sampled.point_data[scalar_name])
            if mag.size != nx * ny:
                raise RuntimeError(f"sample size mismatch: got {mag.size}, expected {nx * ny}")
            grid = mag.reshape(ny, nx).astype(float)
            if "vtkValidPointMask" in sampled.point_data:
                mask = np.asarray(sampled.point_data["vtkValidPointMask"]).ravel().reshape(ny, nx)
                grid = grid.copy()
                grid[mask == 0] = np.nan

    grid = _smooth_grid(grid, sigma=3.5)
    return grid, mask


def _frame_from_layout(layout_roi, mxmin, mxmax, mymin, mymax, pad_frac=0.35, max_aspect=2.5):
    """Keep the heatmap framed on the layout (not the full air-box)."""
    return _union_and_frame(
        *layout_roi, mxmin, mxmax, mymin, mymax,
        layout=None, pad_frac=pad_frac, max_aspect=max_aspect,
    )


def export_slice(
    mesh_path: str,
    outdir: str,
    z_um: Optional[float],
    resolution: int,
    log_scale: bool,
    arrows: bool,
    arrow_count: int,
    quantity: Optional[str],
    scale_to_um: Optional[float],
    roi_xmin_um: Optional[float] = None,
    roi_xmax_um: Optional[float] = None,
    roi_ymin_um: Optional[float] = None,
    roi_ymax_um: Optional[float] = None,
    roi_pad_frac: float = 0.35,
    auto_z: bool = False,
):
    layout_roi = None
    if None not in (roi_xmin_um, roi_xmax_um, roi_ymin_um, roi_ymax_um):
        lx0, lx1 = sorted((float(roi_xmin_um), float(roi_xmax_um)))
        ly0, ly1 = sorted((float(roi_ymin_um), float(roi_ymax_um)))
        layout_roi = (lx0, lx1, ly0, ly1)

    # Fast Z scrubbing from volume cache (optional). Disabled when cache looks
    # like an older broken build; fingerprint still gates reuse.
    if not auto_z and z_um is not None:
        hit = _try_export_from_volume_cache(
            outdir, mesh_path, z_um, log_scale, arrows, layout_roi, resolution
        )
        if hit is not None:
            return hit

    pv = _try_import_pyvista()
    try:
        import numpy as np
    except Exception as exc:
        _die(f"numpy required: {exc}\n  pip install numpy pillow")

    if not os.path.isfile(mesh_path):
        _die(f"input not found: {mesh_path}")
    os.makedirs(outdir, exist_ok=True)

    mesh = pv.read(mesh_path)
    if hasattr(mesh, "n_blocks"):
        for i in range(mesh.n_blocks):
            block = mesh[i]
            if block is not None and getattr(block, "n_points", 0) > 0:
                mesh = block
                break

    if mesh.n_points == 0:
        _die("mesh has no points")

    bounds_native = list(mesh.bounds)
    scale = scale_to_um if scale_to_um is not None else _guess_scale_to_um(bounds_native)
    mxmin, mxmax, mymin, mymax, zmin, zmax = _mesh_bounds_um(mesh, scale)

    if layout_roi is not None:
        xmin, xmax, ymin, ymax = layout_roi
    else:
        xmin, xmax, ymin, ymax = mxmin, mxmax, mymin, mymax

    names = _array_names(mesh)
    scalar_name = _pick_scalar(names, quantity)
    if not scalar_name:
        _die(f"no scalar arrays found in {mesh_path}; arrays={names}")

    if scalar_name in mesh.point_data:
        data_mesh = mesh
    elif scalar_name in mesh.cell_data:
        data_mesh = mesh.cell_data_to_point_data()
    else:
        data_mesh = mesh

    z_clip = z_um if z_um is not None else 0.5 * (zmin + zmax)
    if auto_z or z_um is None:
        z_clip = _auto_z_um(data_mesh, scalar_name, scale, zmin, zmax, xmin, xmax, ymin, ymax)
    z_clip = min(max(z_clip, zmin), zmax)

    # Prefer layout-centered frame. Full-domain "active field" framing makes EM
    # air-box heatmaps look wrong next to a small DUT.
    if layout_roi is not None:
        xmin, xmax, ymin, ymax = _frame_from_layout(
            layout_roi, mxmin, mxmax, mymin, mymax, pad_frac=roi_pad_frac
        )
    else:
        active = _field_active_xy_um(
            data_mesh, scalar_name, scale, z_clip, mxmin, mxmax, mymin, mymax
        )
        if active is not None:
            xmin, xmax, ymin, ymax = _union_and_frame(
                *active, mxmin, mxmax, mymin, mymax, layout=None, pad_frac=0.12
            )
        else:
            xmin, xmax, ymin, ymax = mxmin, mxmax, mymin, mymax

    span_x = max(xmax - xmin, 1e-9)
    span_y = max(ymax - ymin, 1e-9)
    base = max(64, int(resolution))
    if span_x >= span_y:
        nx = base
        ny = max(32, int(round(base * span_y / span_x)))
    else:
        ny = base
        nx = max(32, int(round(base * span_x / span_y)))
    xs = np.linspace(xmin, xmax, nx)
    ys = np.linspace(ymin, ymax, ny)

    try:
        grid_vals, _mask = _sample_z_grid(pv, data_mesh, scalar_name, scale, xs, ys, z_clip)
    except Exception as exc:
        _die(f"slice sample failed: {exc}")

    # Color scale from this slice (valid points only) — avoids posterized global range.
    valid = grid_vals[np.isfinite(grid_vals)]
    if valid.size == 0:
        meta = {
            "version": 1,
            "quantity": scalar_name,
            "z_um": z_clip,
            "zmin_um": zmin,
            "zmax_um": zmax,
            "xmin_um": xmin,
            "xmax_um": xmax,
            "ymin_um": ymin,
            "ymax_um": ymax,
            "png": "field_slice.png",
            "log_scale": bool(log_scale),
            "show_arrows": bool(arrows),
            "arrows": [],
            "status": f"Empty slice at Z={z_clip:.4g} µm",
            "arrays": names,
            "source": os.path.abspath(mesh_path),
            "from_volume_cache": False,
        }
        _atomic_save_png(np.zeros((8, 8, 4), dtype=np.uint8), os.path.join(outdir, "field_slice.png"))
        _atomic_save_json(meta, os.path.join(outdir, "field_slice_meta.json"))
        print(json.dumps({"ok": True, "meta": os.path.join(outdir, "field_slice_meta.json")}))
        return meta

    img_vals = np.flipud(grid_vals)
    # Transparent where NaN (outside mesh).
    finite = np.isfinite(img_vals)
    fill = float(np.nanpercentile(img_vals, 2)) if finite.any() else 0.0
    img_filled = np.where(finite, img_vals, fill)

    local_vmin = float(np.nanpercentile(img_vals, 2))
    local_vmax = float(np.nanpercentile(img_vals, 98))
    rgba, vmin, vmax = _colormap_rgba(img_filled, log_scale, local_vmin, local_vmax)
    rgba = rgba.reshape(img_vals.shape[0], img_vals.shape[1], 4)
    # Restore alpha for out-of-mesh samples.
    alpha = rgba[:, :, 3].astype(np.float32)
    alpha[~np.flipud(finite)] = 0
    rgba[:, :, 3] = alpha.astype(np.uint8)
    rgba = np.ascontiguousarray(rgba)

    png_path = os.path.join(outdir, "field_slice.png")
    _atomic_save_png(rgba, png_path)

    arrow_list = []
    is_thermal = scalar_name.lower() in ("temperature", "temp", "t") or "temp" in scalar_name.lower()
    if arrows and not is_thermal:
        vec_name = _pick_vector(names)
        if vec_name:
            try:
                n_arr = max(4, int(math.sqrt(arrow_count)))
                axs = np.linspace(xmin, xmax, n_arr)
                ays = np.linspace(ymin, ymax, n_arr)
                axx, ayy = np.meshgrid(axs / scale, ays / scale, indexing="xy")
                azz = np.full_like(axx, z_clip / scale)
                apts = np.column_stack([axx.ravel(), ayy.ravel(), azz.ravel()])
                asamp = pv.PolyData(apts).sample(data_mesh)
                vec = np.asarray(asamp.point_data[vec_name], dtype=float)
                mask = None
                if "vtkValidPointMask" in asamp.point_data:
                    mask = np.asarray(asamp.point_data["vtkValidPointMask"]).ravel()
                if vec.ndim == 1:
                    vec = None
                else:
                    vec = vec.reshape(-1, vec.shape[-1])
                if vec is not None and vec.shape[1] >= 2:
                    for i in range(vec.shape[0]):
                        if mask is not None and int(mask[i]) == 0:
                            continue
                        dx, dy = float(vec[i, 0]), float(vec[i, 1])
                        mag = math.hypot(dx, dy)
                        if mag < 1e-30:
                            continue
                        arrow_list.append(
                            {
                                "x_um": float(apts[i, 0] * scale),
                                "y_um": float(apts[i, 1] * scale),
                                "dx": dx / mag,
                                "dy": dy / mag,
                                "mag": mag,
                            }
                        )
            except Exception as exc:
                print(f"field_slice_export: arrows skipped: {exc}", file=sys.stderr)

    meta = {
        "version": 1,
        "quantity": scalar_name,
        "z_um": z_clip,
        "zmin_um": zmin,
        "zmax_um": zmax,
        "xmin_um": xmin,
        "xmax_um": xmax,
        "ymin_um": ymin,
        "ymax_um": ymax,
        "png": "field_slice.png",
        "log_scale": bool(log_scale),
        "show_arrows": bool(arrows),
        "vmin": vmin,
        "vmax": vmax,
        "scale_to_um": scale,
        "arrows": arrow_list,
        "status": "",
        "arrays": names,
        "source": os.path.abspath(mesh_path),
        "from_volume_cache": False,
    }
    meta_path = os.path.join(outdir, "field_slice_meta.json")
    _atomic_save_json(meta, meta_path)
    print(json.dumps({"ok": True, "meta": meta_path, "png": png_path}))
    return meta


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="EMStudio Layout Field Z-clip exporter")
    p.add_argument("--input", "-i", required=True, help="Field dump (.pvd/.vtu/.vtk/.vtr)")
    p.add_argument("--outdir", "-o", required=True, help="Output directory for PNG + meta JSON")
    p.add_argument("--z-um", type=float, default=None, help="Clip Z in micrometres")
    p.add_argument("--resolution", type=int, default=256, help="Grid resolution per side")
    p.add_argument("--log", action="store_true", help="Log10 color scale")
    p.add_argument("--no-arrows", action="store_true", help="Skip vector glyphs")
    p.add_argument("--arrow-count", type=int, default=64)
    p.add_argument("--quantity", default=None, help="Preferred scalar array name")
    p.add_argument("--scale-to-um", type=float, default=None, help="Multiply mesh coords by this to get µm")
    p.add_argument("--xmin-um", type=float, default=None, help="Crop ROI xmin (layout µm)")
    p.add_argument("--xmax-um", type=float, default=None, help="Crop ROI xmax (layout µm)")
    p.add_argument("--ymin-um", type=float, default=None, help="Crop ROI ymin (layout µm, Y-up)")
    p.add_argument("--ymax-um", type=float, default=None, help="Crop ROI ymax (layout µm, Y-up)")
    p.add_argument("--auto-z", action="store_true",
                   help="Pick Z of strongest scalar (e.g. max temperature) inside ROI")
    args = p.parse_args(argv)

    export_slice(
        mesh_path=args.input,
        outdir=args.outdir,
        z_um=args.z_um,
        resolution=args.resolution,
        log_scale=args.log,
        arrows=not args.no_arrows,
        arrow_count=args.arrow_count,
        quantity=args.quantity,
        scale_to_um=args.scale_to_um,
        roi_xmin_um=args.xmin_um,
        roi_xmax_um=args.xmax_um,
        roi_ymin_um=args.ymin_um,
        roi_ymax_um=args.ymax_um,
        auto_z=args.auto_z,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
