"""Runs the golden model scripts (tests/golden) as Python, against stand-in workflow packages.

Run: python -m pytest tests/python/test_golden_scripts.py
Needs numpy only. The Qt golden tests compare the generated script text; this test checks that the
expected scripts are valid models: they run without error (no undefined name, no wrong argument),
and the values the GUI edits reach the workflow calls. The stand-ins in fixtures/golden_standins
export the same names and keep the same signatures as gds2palace / gds2openEMS / openEMS
(via fixtures/workflow_standins.api), record every call and simulate nothing.
"""

import csv
import json
import os
import shutil
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
GOLDEN = os.path.join(REPO, "tests", "golden")
FIXTURES = os.path.join(HERE, "fixtures")
STANDINS = os.path.join(FIXTURES, "golden_standins")

pytest.importorskip("numpy")

# One via port so the openEMS excitation loop calls setupSimulation / runSimulation.
_OPENEMS_PORT = """
simulation_ports.add_port(simulation_setup.simulation_port(
    portnumber=1, voltage=1, port_Z0=50, source_layernum=201,
    from_layername='Metal1', to_layername='TopMetal1', direction='z'))
"""


def required_keywords(tool):
    """Keys marked required in keywords/<tool>.csv (the workflow needs them)."""
    with open(os.path.join(REPO, "keywords", tool + ".csv"), encoding="utf-8") as f:
        return {row[0] for row in csv.reader(f, delimiter="\t") if len(row) > 4 and row[4] == "yes"}


def run_golden(tmp_path, name, inject_openems_port=False):
    """Runs tests/golden/<name> as model.py in tmp_path; returns (process, recorded calls)."""
    model = tmp_path / "model.py"
    text = open(os.path.join(GOLDEN, name), encoding="utf-8").read()
    if inject_openems_port:
        marker = "simulation_ports = simulation_setup.all_simulation_ports()\n"
        assert marker in text, "openEMS golden missing ports init"
        text = text.replace(marker, marker + _OPENEMS_PORT, 1)
    model.write_text(text, encoding="utf-8")
    record = tmp_path / "calls.jsonl"
    env = dict(os.environ)
    # stand-in packages first; fixtures/ so workflow_standins.api is importable
    env["PYTHONPATH"] = os.pathsep.join([STANDINS, FIXTURES])
    env["STANDIN_RECORD"] = str(record)
    proc = subprocess.run([sys.executable, str(model)], cwd=tmp_path, env=env,
                          capture_output=True, text=True, timeout=120)
    calls = []
    if record.exists():
        calls = [json.loads(line) for line in record.read_text(encoding="utf-8").splitlines()]
    return proc, calls


def only(calls, name):
    found = [c["args"] for c in calls if c["call"] == name]
    assert len(found) == 1, f"{name}: {len(found)} calls in {calls}"
    return found[0]


def test_palace_golden_runs(tmp_path):
    proc, calls = run_golden(tmp_path, "tst_palace_golden.py")
    assert proc.returncode == 0, proc.stderr

    # The GUI's top cell and the template's datatypes reach read_gds; the stackup is read.
    gds = only(calls, "read_gds")
    assert gds["filename"] == "<GDS_PATH>"
    assert gds["cellname"] == "t1"
    assert gds["purposelist"] == [0]
    assert only(calls, "read_substrate")["XML_filename"] == "<XML_PATH>"

    # gds2palace gets every required setting.
    palace = only(calls, "create_palace")
    missing = required_keywords("palace") - set(palace["settings_keys"])
    assert not missing, f"settings without required keys: {sorted(missing)}"
    only(calls, "create_run_script")


def test_openems_golden_runs(tmp_path):
    # Inject one port: the stock golden has an empty Ports table, so the excitation loop
    # would never call setupSimulation / runSimulation otherwise.
    proc, calls = run_golden(tmp_path, "tst_openems_golden.py", inject_openems_port=True)
    assert proc.returncode == 0, proc.stderr

    gds = only(calls, "read_gds")
    assert gds["filename"] == "<GDS_PATH>"
    assert gds["cellname"] == "t1"
    assert gds["purposelist"] == [0]
    assert only(calls, "read_substrate")["XML_filename"] == "<XML_PATH>"

    # The GUI edits reach openEMS: numfreq = 123 frequencies in the Touchstone output.
    assert only(calls, "SetBoundaryCond")["BC"] == ["PEC"] * 6
    assert only(calls, "write_snp")["numfreq"] == 123
    # With a port, the workflow core runs (catches wrong kwargs on setup/run).
    only(calls, "setupSimulation")
    only(calls, "runSimulation")
    assert only(calls, "openEMS")["EndCriteria"] is not None


def test_openems_rejects_unknown_init_kwarg(tmp_path):
    """openEMS(**kwargs) must not swallow typos like EndCritera."""
    model = tmp_path / "bad.py"
    model.write_text(
        "from openEMS import openEMS\n"
        "openEMS(EndCritera=1e-4)\n",
        encoding="utf-8")
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join([STANDINS, FIXTURES])
    proc = subprocess.run([sys.executable, str(model)], cwd=tmp_path, env=env,
                          capture_output=True, text=True, timeout=30)
    assert proc.returncode != 0
    assert "EndCritera" in (proc.stderr + proc.stdout)
