"""Build CMIM GDS variants with short outward metal leads + Metal1 frame.

Layout (top view, microns), DUT = original 2.3 um CMIM at origin bbox:
  MIM   (-0.335,0.610)-(1.965,2.910)
  Short stubs (~LEAD_UM) beyond TM1 pad / M5 device edge; ports at stub ends.
  (Long ~30 um rfcmim leads inflated LF C via shunt to Metal1 frame.)

Writes:
  cmim_2u3_flat.gds      - PCell TM1 landing as-is (1.26 um, TM1.a DRC fail)
  cmim_2u3_tm1fix.gds   - TopMetal1 landing enlarged to 1.64 um (TM1.a workaround
                           from IHP-Open-PDK#493); MIM / M5 / Vmim unchanged
  cmim_2u3_open.gds      - same as flat but without MIM (36) and Vmim (129);
                           open fixture for Y_meas - Y_open de-embed
  cmim_2u3_open_tm1fix.gds - open fixture with TM1 1.64 um (match tm1fix DUT)
"""
from pathlib import Path
import gdspy

SRC = Path("cmim_orig.gds")

# Device TM1 from small CMIM PCell (w=l=2.3): 1.26 x 1.26
TM1_PCELL = 1.26
# TM1.a minimum metal width in SG13G2
TM1_FIXED = 1.64

# Stub beyond DUT metal edge (um). Keep small to limit feeder shunt C at 1 GHz.
LEAD_UM = 2.0
# M5 device edge from PCell (right side of Metal5 landing under MIM stack)
M5_DUT_XMAX = 2.565

# MIM plate + Vmim - drop these for open-fixture de-embed
MIM_LAYERS = {36, 129}


def collect_device(src_top, drop_layers=()):
    device_polys = []
    drop = set(drop_layers)

    def collect(cell, xoff=0.0, yoff=0.0):
        for p in cell.polygons:
            L = int(p.layers[0])
            if L in (201, 202, 250, 251, 8) or L in drop:
                continue
            pts = []
            for ring in p.polygons:
                pts.append([(float(x) + xoff, float(y) + yoff) for x, y in ring])
            poly = gdspy.Polygon(pts[0], layer=L, datatype=int(p.datatypes[0]))
            device_polys.append(poly)
        for ref in cell.references:
            collect(ref.ref_cell, xoff + float(ref.origin[0]), yoff + float(ref.origin[1]))

    collect(src_top)
    return device_polys


def tm1_device_bbox(device_polys):
    """Bounding box of the compact TopMetal1 pad (not a long lead)."""
    boxes = []
    for p in device_polys:
        if int(p.layers[0]) != 126:
            continue
        bb = p.get_bounding_box()
        w = float(bb[1][0] - bb[0][0])
        h = float(bb[1][1] - bb[0][1])
        if w < 5.0 and h < 5.0:
            boxes.append(bb)
    if not boxes:
        raise RuntimeError("no compact TopMetal1 polygon found")
    # Prefer the pad matching PCell size
    boxes.sort(key=lambda b: abs((b[1][0] - b[0][0]) - TM1_PCELL)
               + abs((b[1][1] - b[0][1]) - TM1_PCELL))
    return boxes[0]


def build(dst: Path, tm1_size: float, open_fixture: bool = False):
    g_src = gdspy.GdsLibrary(infile=str(SRC))
    drop = MIM_LAYERS if open_fixture else ()
    device_polys = collect_device(g_src.cells["TOP"], drop_layers=drop)
    bb = tm1_device_bbox(device_polys)
    cx = 0.5 * (float(bb[0][0]) + float(bb[1][0]))
    cy = 0.5 * (float(bb[0][1]) + float(bb[1][1]))
    half = 0.5 * tm1_size
    tm1_xmin, tm1_xmax = cx - half, cx + half
    y0, y1 = cy - half, cy + half

    gdspy.current_library = gdspy.GdsLibrary()
    lib = gdspy.current_library
    c = lib.new_cell("TOP")
    for p in device_polys:
        if int(p.layers[0]) == 126:
            # Drop PCell TM1 pad; replaced below (and no long lead yet in collect).
            pbb = p.get_bounding_box()
            pw = float(pbb[1][0] - pbb[0][0])
            ph = float(pbb[1][1] - pbb[0][1])
            if pw < 5.0 and ph < 5.0:
                continue
        c.add(p)

    tm1_x_end = tm1_xmin - LEAD_UM
    m5_x_end = M5_DUT_XMAX + LEAD_UM
    feeder1 = tm1_xmin - tm1_x_end   # == LEAD_UM
    feeder2 = m5_x_end - M5_DUT_XMAX  # == LEAD_UM

    # Enlarged (or original-size) TM1 landing + short left stub
    c.add(gdspy.Rectangle((tm1_xmin, y0), (tm1_xmax, y1), layer=126))
    c.add(gdspy.Rectangle((tm1_x_end, y0), (tm1_xmin, y1), layer=126))
    # M5 short right stub - height matched to TM1 lead for symmetric ports
    c.add(gdspy.Rectangle((M5_DUT_XMAX, y0), (m5_x_end, y1), layer=67))

    # Compact Metal1 frame / reference (hole under DUT); margin past stub ends
    frame_pad = 1.0
    c.add(gdspy.Rectangle((tm1_x_end - frame_pad, y0 - 2), (-0.5, y1 + 2), layer=8))
    c.add(gdspy.Rectangle((1.8, y0 - 2), (m5_x_end + frame_pad, y1 + 2), layer=8))
    c.add(gdspy.Rectangle((-0.5, y1 + 0.5), (1.8, y1 + 2), layer=8))
    c.add(gdspy.Rectangle((-0.5, y0 - 2), (1.8, y0 - 0.5), layer=8))

    # No MIM_Diel GDS brick: Palace stackup uses ~24 nm gap with SiO2 background.

    # Zero-width vertical line ports at stub ends
    c.add(gdspy.Rectangle((tm1_x_end, y0), (tm1_x_end, y1), layer=201))
    c.add(gdspy.Rectangle((m5_x_end, y0), (m5_x_end, y1), layer=202))

    layers = sorted({int(p.layers[0]) for p in c.polygons})
    tag = " OPEN" if open_fixture else ""
    print(f"{dst.name}:{tag} TM1={tm1_size} um @ center ({cx:.4f},{cy:.4f}) "
          f"pad=({tm1_xmin:.4f},{y0:.4f})-({tm1_xmax:.4f},{y1:.4f})")
    print(f"  ports x=({tm1_x_end:.4f}, {m5_x_end:.4f})  "
          f"feeder_um=({feeder1:.4f}, {feeder2:.4f})  layers={layers}")
    lib.write_gds(str(dst))
    print("wrote", dst)
    return feeder1, feeder2


if __name__ == "__main__":
    print(f"LEAD_UM={LEAD_UM}")
    build(Path("cmim_2u3_flat.gds"), TM1_PCELL)
    build(Path("cmim_2u3_tm1fix.gds"), TM1_FIXED)
    build(Path("cmim_2u3_open.gds"), TM1_PCELL, open_fixture=True)
    build(Path("cmim_2u3_open_tm1fix.gds"), TM1_FIXED, open_fixture=True)
