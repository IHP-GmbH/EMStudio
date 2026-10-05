"""Runs the golden model scripts (tests/golden) as Python, against stand-in workflow packages.

Run: python -m pytest tests/python/test_golden_scripts.py
Needs numpy only. The Qt golden tests compare the generated script text; this test checks that the
expected scripts are valid models: they run without error (no undefined name, no wrong argument),
and the values the GUI edits reach the workflow calls. The stand-ins in fixtures/golden_standins
export the same names and keep the same signatures as gds2palace / gds2openEMS / openEMS, record
every call and simulate nothing.
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
STANDINS = os.path.join(HERE, "fixtures", "golden_standins")

pytest.importorskip("numpy")


def required_keywords(tool):
    """Keys marked required in keywords/<tool>.csv (the workflow needs them)."""
    with open(os.path.join(REPO, "keywords", tool + ".csv"), encoding="utf-8") as f:
        return {row[0] for row in csv.reader(f, delimiter="\t") if len(row) > 4 and row[4] == "yes"}


def run_golden(tmp_path, name):
    """Runs tests/golden/<name> as model.py in tmp_path; returns (process, recorded calls)."""
    model = tmp_path / "model.py"
    shutil.copy(os.path.join(GOLDEN, name), model)
    record = tmp_path / "calls.jsonl"
    env = dict(os.environ)
    env["PYTHONPATH"] = STANDINS
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
    proc, calls = run_golden(tmp_path, "tst_openems_golden.py")
    assert proc.returncode == 0, proc.stderr

    gds = only(calls, "read_gds")
    assert gds["filename"] == "<GDS_PATH>"
    assert gds["cellname"] == "t1"
    assert gds["purposelist"] == [0]
    assert only(calls, "read_substrate")["XML_filename"] == "<XML_PATH>"

    # The GUI edits reach openEMS: numfreq = 123 frequencies in the Touchstone output.
    assert only(calls, "SetBoundaryCond")["BC"] == ["PEC"] * 6
    assert only(calls, "write_snp")["numfreq"] == 123
