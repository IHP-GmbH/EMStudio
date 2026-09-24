# SPDX-License-Identifier: GPL-3.0-or-later
"""Batch GDS geometry tweak for EMStudio (runs under KLayout: klayout -b -r …).

Creates ``<input>.bak`` once (keeps the first backup), then overwrites the GDS.

Supported ops (combine as needed):
  - scale about top-cell bbox centre (XY)
  - translate by dx/dy [µm]
  - diameter-style: scale = (current_d + delta) / current_d

KLayout invocation (variables via -rd):
  klayout_app -b -r klayout_modify_gds.py \\
    -rd input=C:/path/file.gds \\
    -rd cell=TOP \\
    -rd scale=1.023 \\
    -rd dx_um=0 -rd dy_um=0 \\
    -rd flatten=1

Or diameter helper:
    -rd current_diameter_um=88 -rd diameter_delta_um=2
"""

from __future__ import annotations

import os
import shutil
import sys
import traceback


def _rd(name: str, default=None):
    """Read a KLayout ``-rd name=value`` global, else default."""
    if name in globals():
        return globals()[name]
    # Allow plain CLI testing: python klayout_modify_gds.py --input=... (no pya)
    for a in sys.argv[1:]:
        if a.startswith(f"--{name}=") or a.startswith(f"-rd{name}="):
            return a.split("=", 1)[1]
        if a.startswith("-rd") and "=" in a:
            # -rdinput=foo style
            key, _, val = a[3:].partition("=")
            if key == name:
                return val
    return default


def _as_float(v, default=0.0) -> float:
    if v is None or v == "":
        return float(default)
    return float(v)


def _as_bool(v, default=True) -> bool:
    if v is None or v == "":
        return default
    s = str(v).strip().lower()
    if s in ("0", "false", "no", "off"):
        return False
    if s in ("1", "true", "yes", "on"):
        return True
    return default


def main() -> int:
    try:
        import pya  # type: ignore
    except ImportError:
        print("ERROR: pya not available — run this script with KLayout (-b -r), not system Python.",
              file=sys.stderr)
        return 2

    inp = str(_rd("input", "") or "").strip().strip('"')
    if not inp or not os.path.isfile(inp):
        print(f"ERROR: input GDS not found: {inp!r}", file=sys.stderr)
        return 2

    out = str(_rd("output", "") or "").strip().strip('"')
    if not out:
        out = inp

    cell_name = str(_rd("cell", "") or "").strip()
    scale = _as_float(_rd("scale", 1.0), 1.0)
    dx_um = _as_float(_rd("dx_um", 0.0), 0.0)
    dy_um = _as_float(_rd("dy_um", 0.0), 0.0)
    flatten = _as_bool(_rd("flatten", 1), True)

    cur_d = _rd("current_diameter_um", None)
    d_delta = _rd("diameter_delta_um", None)
    if cur_d not in (None, "") and d_delta not in (None, ""):
        d0 = _as_float(cur_d)
        dd = _as_float(d_delta)
        if d0 <= 0:
            print("ERROR: current_diameter_um must be > 0", file=sys.stderr)
            return 2
        scale = (d0 + dd) / d0
        print(f"INFO: diameter {d0} + {dd} µm → scale={scale:.8g}")

    if abs(scale - 1.0) < 1e-15 and abs(dx_um) < 1e-15 and abs(dy_um) < 1e-15:
        print("ERROR: nothing to do (scale=1 and dx=dy=0)", file=sys.stderr)
        return 2

    bak = inp + ".bak"
    if not os.path.isfile(bak):
        shutil.copy2(inp, bak)
        print(f"INFO: created backup {bak}")
    else:
        print(f"INFO: keeping existing backup {bak}")

    ly = pya.Layout()
    ly.read(inp)
    dbu = ly.dbu

    cell = None
    if cell_name:
        try:
            cell = ly.cell(cell_name)
        except Exception:
            cell = None
        if cell is None:
            # try case-insensitive
            for c in ly.each_cell():
                if c.name.lower() == cell_name.lower():
                    cell = c
                    break
    if cell is None:
        tops = [c for c in ly.each_cell() if c.is_top()]
        cell = tops[0] if tops else ly.cell(ly.cells() - 1)

    if cell is None:
        print("ERROR: no cell found", file=sys.stderr)
        return 2

    print(f"INFO: cell={cell.name} dbu={dbu} scale={scale} dx_um={dx_um} dy_um={dy_um}")

    if flatten:
        # Avoid double-transform of shared child cells.
        try:
            cell.flatten(-1)
            print("INFO: flattened top cell hierarchy")
        except Exception as exc:
            print(f"WARN: flatten failed ({exc}); transforming as-is")

    bbox = cell.bbox()
    if bbox.empty():
        print("ERROR: empty cell bbox", file=sys.stderr)
        return 2

    cx = bbox.center().x
    cy = bbox.center().y

    # ICplxTrans: p' = mag * R * p + (x, y)  → scale about (cx,cy):
    # p' = scale * p + c * (1 - scale)
    x_off = int(round(cx * (1.0 - scale)))
    y_off = int(round(cy * (1.0 - scale)))
    tr = pya.ICplxTrans(scale, 0.0, False, x_off, y_off)

    if abs(dx_um) > 0 or abs(dy_um) > 0:
        tr = pya.ICplxTrans(1.0, 0.0, False,
                            int(round(dx_um / dbu)),
                            int(round(dy_um / dbu))) * tr

    cell.transform(tr)

    # Atomic-ish write: temp then replace
    tmp = out + ".tmp.gds"
    ly.write(tmp)
    if os.path.abspath(out) == os.path.abspath(inp) or os.path.exists(out):
        os.replace(tmp, out)
    else:
        shutil.move(tmp, out)

    print(f"OK: wrote {out}")
    print(f"META: cell={cell.name} scale={scale:.8g} bak={bak}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        sys.exit(1)
