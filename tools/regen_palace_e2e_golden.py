#!/usr/bin/env python3
"""Build tests/golden/palace_e2e/line_viaport.s2p with gds2palace + Palace (no EMStudio GUI).

Requires:
  - PALACE_INSTALL_PATH pointing at a tree with bin/palace
  - gds2palace installed (pip install gds2palace==0.5.2)
  - combine_extend_snp.py next to this repo's scripts/
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# shutil used for which(mpirun) fallback

ROOT = Path(__file__).resolve().parents[1]
GDS = ROOT / "tests" / "golden" / "line_simple_viaport.gds"
XML = ROOT / "tests" / "golden" / "SG13G2_200um.xml"
OUT = ROOT / "tests" / "golden" / "palace_e2e" / "line_viaport.s2p"
COMBINE = ROOT / "scripts" / "combine_extend_snp.py"

MODEL = r'''
from gds2palace import *
import os

start_simulation = False
run_command = ['./run_sim']
gds_filename = "line_simple_viaport.gds"
cellname = "t1"
XML_filename = "SG13G2_200um.xml"
script_path = utilities.get_script_path(__file__)
model_basename = utilities.get_basename(__file__)
sim_path = utilities.create_sim_path(script_path, model_basename)
print("Simulation data directory: ", sim_path)
modelDir = os.path.dirname(os.path.abspath(__file__))
os.chdir(modelDir)

settings = {}
settings["purpose"] = [0]
settings["preprocess_gds"] = True
settings["merge_polygon_size"] = 0.5
settings["unit"] = 1e-6
# Tiny smoke case: two via-port rectangles, one frequency, coarse mesh (whole E2E ≤ ~5 min).
settings["margin"] = 10
settings["fstart"] = 5e9
settings["fstop"] = 5e9
settings["fstep"] = 5e9
settings["fpoint"] = []
settings["fdump"] = []
settings["boundary"] = ["ABC", "ABC", "ABC", "ABC", "ABC", "ABC"]
settings["refined_cellsize"] = 20
settings["cells_per_wavelength"] = 10
settings["meshsize_max"] = 200
settings["adaptive_mesh_iterations"] = 0
settings["no_gui"] = True  # headless CI / regen

simulation_ports = simulation_setup.all_simulation_ports()
simulation_ports.add_port(simulation_setup.simulation_port(
    portnumber=1, voltage=1, port_Z0=50, source_layernum=201,
    from_layername="Metal1", to_layername="TopMetal2", direction="z"))
simulation_ports.add_port(simulation_setup.simulation_port(
    portnumber=2, voltage=1, port_Z0=50, source_layernum=202,
    from_layername="Metal1", to_layername="TopMetal2", direction="z"))
variable_overrides = {}
materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate(
    XML_filename, variable_overrides=variable_overrides
)
layernumbers = metals_list.getlayernumbers()
layernumbers.extend(simulation_ports.portlayers)
allpolygons = gds_reader.read_gds(
    gds_filename,
    layernumbers,
    purposelist=settings["purpose"],
    metals_list=metals_list,
    preprocess=settings["preprocess_gds"],
    merge_polygon_size=settings["merge_polygon_size"],
    cellname=cellname,
)
settings["simulation_ports"] = simulation_ports
settings["materials_list"] = materials_list
settings["dielectrics_list"] = dielectrics_list
settings["metals_list"] = metals_list
settings["layernumbers"] = layernumbers
settings["allpolygons"] = allpolygons
settings["sim_path"] = sim_path
settings["model_basename"] = model_basename
excite_ports = simulation_ports.all_active_excitations()
config_name, data_dir = simulation_setup.create_palace(excite_ports, settings)
utilities.create_run_script(sim_path)
'''


def main() -> int:
    version = os.environ.get("PALACE_CI_VERSION", "0.16.0").strip()
    image = os.environ.get(
        "PALACE_CI_DOCKER_IMAGE", f"ghcr.io/benvial/palace:v{version}"
    ).strip()
    if not GDS.is_file() or not XML.is_file():
        print("golden GDS/XML missing", file=sys.stderr)
        return 2
    if not COMBINE.is_file():
        print(f"missing {COMBINE}", file=sys.stderr)
        return 2
    if shutil.which("docker") is None:
        print("docker is required to run Palace for the golden", file=sys.stderr)
        return 2

    work = Path(tempfile.mkdtemp(prefix="palace_e2e_golden_"))
    try:
        shutil.copy(GDS, work / "line_simple_viaport.gds")
        shutil.copy(XML, work / "SG13G2_200um.xml")
        model = work / "palace_e2e_line.py"
        model.write_text(MODEL, encoding="utf-8")

        print("Running gds2palace …", flush=True)
        subprocess.check_call([sys.executable, str(model)], cwd=work)

        run_dir = work / "palace_model" / "palace_e2e_line_data"
        cfg = run_dir / "config.json"
        if not cfg.is_file():
            candidates = list((work / "palace_model").glob("*_data/config.json"))
            if not candidates:
                print("config.json not found under palace_model/", file=sys.stderr)
                return 3
            cfg = candidates[0]
            run_dir = cfg.parent

        print(f"Running Palace in Docker ({image}) on {cfg.name} …", flush=True)
        # Hard cap: rectangle smoke golden (mesh+solve+snp) under ~5 minutes on CI.
        # Match host uid so combine_extend_snp can rewrite Touchstone in the mount.
        uid = os.getuid() if hasattr(os, "getuid") else 1001
        gid = os.getgid() if hasattr(os, "getgid") else 1001
        docker_cmd = [
            "docker",
            "run",
            "--rm",
            "-u",
            f"{uid}:{gid}",
            "-v",
            f"{run_dir.resolve()}:/sim",
            "-w",
            "/sim",
            image,
            "palace",
            cfg.name,
        ]
        if shutil.which("timeout"):
            docker_cmd = ["timeout", "--signal=KILL", "240"] + docker_cmd
        subprocess.check_call(docker_cmd)
        # Best-effort if the image ignored -u (root-owned files).
        for p in run_dir.rglob("*"):
            try:
                p.chmod(p.stat().st_mode | 0o666 if p.is_file() else p.stat().st_mode | 0o777)
            except OSError:
                pass

        print("CSV → Touchstone …", flush=True)
        subprocess.check_call([sys.executable, str(COMBINE)], cwd=run_dir)

        snps = sorted(run_dir.rglob("*.s2p")) + sorted(run_dir.rglob("*.S2P"))
        snps = [p for p in snps if "deembed" not in p.name.lower() and "_dc" not in p.name.lower()]
        if not snps:
            print("no .s2p produced", file=sys.stderr)
            return 4

        OUT.parent.mkdir(parents=True, exist_ok=True)
        text = snps[0].read_text(encoding="utf-8", errors="replace")
        header = (
            f"! Palace E2E golden for line_simple_viaport\n"
            f"! palace={version} image={image} "
            f"gds2palace=0.5.2 f=5GHz (single point, coarse mesh)\n"
        )
        if not text.lstrip().startswith("!"):
            text = header + text
        else:
            text = header + text
        OUT.write_text(text, encoding="utf-8")
        print(f"Wrote {OUT}", flush=True)
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
