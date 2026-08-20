#!/usr/bin/env python3
"""CLI wrapper for ADS Momentum <-> EMStudio stackup XML conversion.

Import (Volker setupEM momentum_import):
  python ads_convert.py import-subst <file.subst> <out.xml> [--air 300]
  python ads_convert.py import-ltd   <file.ltd>   <out.xml> [--air 300]

Export (basic EMStudio XML -> ADS *.subst + materials.matdb):
  python ads_convert.py export-subst <in.xml> <out.subst>
  (writes materials.matdb next to out.subst)
"""

from __future__ import annotations

import argparse
import os
import sys
import xml.etree.ElementTree as ET

# Allow running from any cwd: put this script's directory on sys.path
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import momentum_import  # noqa: E402
import stackup_writer  # noqa: E402


def _print_warnings(warnings):
    for w in warnings or []:
        print(f"WARNING: {w}", file=sys.stderr)


def cmd_import_subst(args):
    matdb = os.path.join(os.path.dirname(os.path.abspath(args.input)), "materials.matdb")
    if not os.path.isfile(matdb):
        print(f"ERROR: materials.matdb not found next to {args.input}", file=sys.stderr)
        return 2
    result = momentum_import.import_subst(args.input, matdb, args.air)
    _print_warnings(result.warnings)
    stackup_writer.save_stackup_tree(result.tree, args.output)
    print(args.output)
    return 0


def cmd_import_ltd(args):
    result = momentum_import.import_ltd(args.input, args.air)
    _print_warnings(result.warnings)
    stackup_writer.save_stackup_tree(result.tree, args.output)
    print(args.output)
    return 0


def _color_hex(mat_el):
    c = (mat_el.get("Color") or "").strip().lstrip("#")
    return c


def cmd_export_subst(args):
    """Export a simplified ADS *.subst + materials.matdb from stackup XML.

    This is a best-effort reverse of the import path for legacy absolute-Z
    stackups (schema 2.0). Reference-relative / Variables (3.0/3.1) are resolved
    by EMStudio before export when possible; here we use Thickness / Zmin / Zmax
    attribute values as written in the XML.
    """
    tree = ET.parse(args.input)
    root = tree.getroot()

    materials_el = root.find("Materials")
    dielectrics_el = root.find("ELayers/Dielectrics")
    layers_el = root.find("ELayers/Layers")
    if materials_el is None or dielectrics_el is None or layers_el is None:
        print("ERROR: XML missing Materials/ELayers sections", file=sys.stderr)
        return 2

    # --- materials.matdb ---
    matdb_root = ET.Element("MaterialDB")
    for mat in materials_el.findall("Material"):
        name = mat.get("Name") or ""
        mtype = (mat.get("Type") or "Dielectric").lower()
        if mtype == "conductor" or mtype == "resistor":
            el = ET.SubElement(matdb_root, "Conductor", name=name)
            if mtype == "resistor" and mat.get("Rs"):
                el.set("real", f"{mat.get('Rs')} Ohm/Sq")
            else:
                cond = mat.get("Conductivity") or "0"
                el.set("real", f"{cond} Siemens/m")
        elif mtype == "semiconductor":
            el = ET.SubElement(matdb_root, "Semiconductor", name=name)
            el.set("er_real", mat.get("Permittivity") or "11.9")
            cond = float(mat.get("Conductivity") or "0")
            # ADS uses Ohm.cm resistivity; sigma[S/m] = 1/(rho_ohm_cm * 0.01)
            rho = (1.0 / (cond * 0.01)) if cond > 0 else 0.0
            el.set("resistivity", f"{rho:g} Ohm.cm")
        else:
            el = ET.SubElement(matdb_root, "Dielectric", name=name)
            el.set("er_real", mat.get("Permittivity") or "1")
            el.set("er_loss", mat.get("DielectricLossTangent") or "0")

    # Dielectrics are stored top-to-bottom in XML; ADS subst expects bottom-to-top order
    diels = list(dielectrics_el.findall("Dielectric"))
    diels_bottom_up = list(reversed(diels))

    subst_root = ET.Element("substrate")
    # Dielectric slabs
    for d in diels_bottom_up:
        el = ET.SubElement(subst_root, "material")
        el.set("materialname", d.get("Material") or d.get("Name") or "")
        el.set("thick", d.get("Thickness") or "0")
        el.set("thickunit", "micron")

    # Ensure open top boundary (zero-thickness top) like Momentum exports often have —
    # import replaces last ~0 thickness with air_thickness. We append a 0-thick AIR.
    top = ET.SubElement(subst_root, "material")
    top.set("materialname", "AIR")
    top.set("thick", "0")
    top.set("thickunit", "micron")

    # Interfaces + layers: map absolute Z to slab indices (best-effort)
    # Build cumulative z bottoms/tops for bottom-up dielectrics
    z = 0.0
    interface_z = []  # top of each slab
    for d in diels_bottom_up:
        try:
            t = float(d.get("Thickness") or "0")
        except ValueError:
            t = 0.0
        z += t
        interface_z.append(z)

    offset_el = layers_el.find("Substrate")
    try:
        offset = float(offset_el.get("Offset")) if offset_el is not None else 0.0
    except (TypeError, ValueError):
        offset = 0.0

    def nearest_interface(zabs):
        # Layer Z in XML is typically relative to substrate top (= offset frame)
        z_bottom_frame = zabs + offset
        best_i, best_d = 0, 1e99
        for i, zi in enumerate(interface_z):
            dlt = abs(zi - z_bottom_frame)
            if dlt < best_d:
                best_i, best_d = i, dlt
        return best_i

    for lay in layers_el.findall("Layer"):
        name = lay.get("Name") or ""
        ltype = (lay.get("Type") or "conductor").lower()
        try:
            zmin = float(lay.get("Zmin") or "0")
            zmax = float(lay.get("Zmax") or "0")
        except ValueError:
            continue
        thick = abs(zmax - zmin)
        idx = nearest_interface(zmin)
        if ltype == "via":
            el = ET.SubElement(subst_root, "via")
            el.set("materialname", lay.get("Material") or name)
            el.set("name", name)
            el.set("layer", lay.get("Layer") or "0")
            el.set("index1", str(idx))
            # second interface: nearest to zmax
            el.set("index2", str(nearest_interface(zmax)))
        else:
            el = ET.SubElement(subst_root, "layer")
            el.set("materialname", lay.get("Material") or name)
            el.set("name", name)
            el.set("layer", lay.get("Layer") or "0")
            el.set("index", str(idx))
            el.set("thick", f"{thick:.6g}")
            el.set("thickunit", "micron")
            el.set("expand", "0")

    out_subst = os.path.abspath(args.output)
    out_dir = os.path.dirname(out_subst)
    os.makedirs(out_dir, exist_ok=True)
    matdb_path = os.path.join(out_dir, "materials.matdb")

    try:
        ET.indent(matdb_root, space="  ")
        ET.indent(subst_root, space="  ")
    except AttributeError:
        pass

    ET.ElementTree(matdb_root).write(matdb_path, encoding="utf-8", xml_declaration=True)
    ET.ElementTree(subst_root).write(out_subst, encoding="utf-8", xml_declaration=True)
    print(out_subst)
    print(matdb_path)
    print("WARNING: ADS export is best-effort for absolute-Z stackups; verify in ADS.",
          file=sys.stderr)
    return 0


def main():
    p = argparse.ArgumentParser(description="ADS Momentum stackup conversion for EMStudio")
    sub = p.add_subparsers(dest="cmd", required=True)

    p1 = sub.add_parser("import-subst", help="Import *.subst + materials.matdb -> XML")
    p1.add_argument("input")
    p1.add_argument("output")
    p1.add_argument("--air", type=float, default=300.0, help="Top AIR thickness [um]")
    p1.set_defaults(func=cmd_import_subst)

    p2 = sub.add_parser("import-ltd", help="Import *.ltd -> XML")
    p2.add_argument("input")
    p2.add_argument("output")
    p2.add_argument("--air", type=float, default=300.0, help="Top AIR thickness [um]")
    p2.set_defaults(func=cmd_import_ltd)

    p3 = sub.add_parser("export-subst", help="Export XML -> *.subst + materials.matdb")
    p3.add_argument("input")
    p3.add_argument("output")
    p3.set_defaults(func=cmd_export_subst)

    args = p.parse_args()
    try:
        return args.func(args)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main() or 0)
