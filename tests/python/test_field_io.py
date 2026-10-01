"""Tests for scripts/field_io.py on small synthetic field dumps.

Run: python -m pytest tests/python
Needs pyvista (same host Python as the Field viewer).
"""

import json
import os
import sys

import numpy as np
import pytest

pv = pytest.importorskip("pyvista")

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import field_io  # noqa: E402


def _grid(scale=1.0):
    g = pv.ImageData(dimensions=(4, 3, 3), spacing=(10 * scale,) * 3,
                     origin=(-15 * scale, -10 * scale, 0)).cast_to_unstructured_grid()
    return g


def _em_mesh(palace=True, boundary=False):
    g = _grid()
    n = g.n_points
    re = np.tile([3.0, 0.0, 0.0], (n, 1))
    im = np.tile([0.0, 4.0, 0.0], (n, 1))
    re[0] = [30.0, 0.0, 0.0]
    if palace:
        g.point_data["E_real"] = re
        g.point_data["E_imag"] = im
        g.point_data["B_real"] = re * 1e-9
        g.point_data["B_imag"] = im * 1e-9
        g.point_data["S"] = re
        g.point_data["U_e"] = np.linspace(1, 2, n)
        if boundary:
            g.point_data["Q_s_real"] = np.linspace(1, 2, n)
            g.point_data["Q_s_imag"] = np.zeros(n)
        g.cell_data["attribute"] = np.ones(g.n_cells, dtype=int)
    else:
        g.point_data["electric field re"] = re
        g.point_data["electric field im"] = im
        g.point_data["magnetic field strength re"] = re
        g.point_data["magnetic field strength im"] = im
        g.cell_data["GeometryIds"] = np.ones(g.n_cells, dtype=int)
    return g


def _write_pvd(path, entries):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    rows = "\n".join(
        f'    <DataSet timestep="{ts}" part="0" file="{f}"/>' for ts, f in entries)
    with open(path, "w") as fh:
        fh.write('<?xml version="1.0"?>\n<VTKFile type="Collection" version="0.1">\n'
                 f"  <Collection>\n{rows}\n  </Collection>\n</VTKFile>\n")


def _palace_run(root, with_amr=False):
    run = root / "model_data"
    out = run / "output" / "model" / "paraview"
    (run).mkdir(parents=True)
    (run / "config.json").write_text(json.dumps({"Problem": {"Output": "output/model"}}))

    def dump(base, name, boundary):
        d = base / name / "Cycle000000"
        d.mkdir(parents=True)
        _em_mesh(boundary=boundary).save(str(d / "data.vtu"))
        geo = _grid()
        geo.cell_data["Indicator"] = np.ones(geo.n_cells)
        geo.cell_data["Rank"] = np.zeros(geo.n_cells)
        d2 = base / name / "Cycle000001"
        d2.mkdir(parents=True)
        geo.save(str(d2 / "data.vtu"))
        _write_pvd(str(base / name / f"{name}.pvd"),
                   [(6, "Cycle000000/data.vtu"), (99, "Cycle000001/data.vtu")])

    dump(out, "driven", False)
    dump(out, "driven_boundary", True)
    if with_amr:
        dump(run / "output" / "model" / "iteration1" / "paraview", "driven", False)
    return run


def test_palace_discovery_labels_and_cycles(tmp_path):
    run = _palace_run(tmp_path, with_amr=True)
    files = field_io.discover(str(run), field_io.PALACE)
    assert len(files) == 3
    assert all(f.endswith(".pvd") for f in files)
    amr = [f for f in files if field_io.is_amr_iteration_path(f)]
    assert len(amr) == 1

    final = [f for f in files if f not in amr]
    labels = field_io.build_file_labels(final, field_io.PALACE)
    assert sorted(labels.values()) == ["\U0001F532 driven_boundary.pvd", "\U0001F9CA driven.pvd"]

    driven = next(f for f in final if f.endswith(os.sep + "driven.pvd"))
    infos = field_io.cycle_infos(driven, field_io.PALACE)
    assert [c["label"] for c in infos] == ["6 GHz (cycle 1)", "geometry (cycle 2)"]

    mesh, idx, n = field_io.load(driven, 1)
    assert (idx, n) == (1, 2)
    assert field_io.visible_point_arrays(mesh) == []
    mesh, idx, n = field_io.load(driven, 0)
    assert "E_real" in mesh.point_data


def test_palace_listing_json(tmp_path):
    run = _palace_run(tmp_path)
    data = field_io.list_run(str(run), field_io.PALACE)
    assert data["scale_to_um"] == 1.0
    assert len(data["files"]) == 2
    assert data["files"][0]["cycles"][0]["label"] == "6 GHz (cycle 1)"
    json.dumps(data)  # serialisable


def test_missing_run_dir_is_empty(tmp_path):
    for src in field_io.SOURCES:
        assert field_io.discover(str(tmp_path / "nope"), src) == []
    data = field_io.list_run(str(tmp_path / "nope"), field_io.PALACE)
    assert data["files"] == [] and data["hint"]


def test_magnitudes_and_field_list():
    mesh = _em_mesh(boundary=True)
    added = field_io.attach_derived(mesh, field_io.PALACE)
    assert set(added) == {"|E|", "|B|", "|Q_s|"}
    # |E| = |(3,0,0) + j(0,4,0)| = 5, first point |(30,4,0)|
    assert np.isclose(mesh["|E|"][1], 5.0)
    assert np.isclose(mesh["|E|"][0], np.hypot(30.0, 4.0))

    entries = field_io.field_entries(mesh)
    names = [e.name for e in entries]
    assert names[:3] == ["|E|", "|B|", "|Q_s|"]
    assert names.index("E_real") < names.index("E_imag") < names.index("S") < names.index("U_e")
    assert "attribute" not in names
    e = entries[0]
    assert e.display.startswith("|E| – electric field") and "[V/m]" in e.display
    assert e.title == "E magnitude [V/m]"
    assert field_io.pick_default(mesh, field_io.PALACE) == ("|E|", field_io.EMSTUDIO_CMAP, True)


def test_unknown_arrays_kept_last():
    mesh = _em_mesh()
    mesh.point_data["my_custom"] = np.zeros(mesh.n_points)
    field_io.attach_derived(mesh)
    entries = field_io.field_entries(mesh)
    assert entries[-1].name == "my_custom"
    assert entries[-1].display == "my_custom"


def test_elmer_em_pvtu_preferred_and_frequency_labels(tmp_path):
    run = tmp_path / "line_data"
    mesh_dir = run / "mesh"
    mesh_dir.mkdir(parents=True)
    (run / "frequencies.dat").write_text("1  5.000e+09\n2  7.500e+09\n3  1.000e+10\n")
    m = _em_mesh(palace=False)
    for step in (1, 2, 3):
        pieces = []
        for k in (1, 2):
            name = f"fields_2np{k}_t{step:04d}.vtu"
            m.save(str(mesh_dir / name))
            pieces.append(name)
        arrays = "".join(
            f'<PDataArray type="Float64" Name="{n}" NumberOfComponents="3"/>'
            for n in m.point_data.keys())
        src = "".join(f'<Piece Source="{p}"/>' for p in pieces)
        (mesh_dir / f"fields_t{step:04d}.pvtu").write_text(
            '<?xml version="1.0"?><VTKFile type="PUnstructuredGrid" version="0.1">'
            f'<PUnstructuredGrid GhostLevel="0"><PPointData>{arrays}</PPointData>'
            '<PPoints><PDataArray type="Float64" NumberOfComponents="3"/></PPoints>'
            f"{src}</PUnstructuredGrid></VTKFile>")
    files = field_io.discover(str(run), field_io.ELMER_EM)
    assert [os.path.basename(f) for f in files] == [
        "fields_t0001.pvtu", "fields_t0002.pvtu", "fields_t0003.pvtu"]
    labels = field_io.build_file_labels(files, field_io.ELMER_EM)
    assert [labels[f] for f in files] == [
        "fields_t0001.pvtu - 5 GHz", "fields_t0002.pvtu - 7.5 GHz", "fields_t0003.pvtu - 10 GHz"]

    mesh, _, _ = field_io.load(files[1])
    added = field_io.attach_derived(mesh, field_io.ELMER_EM)
    assert set(added) == {"|E|", "|H|"}
    assert field_io.pick_default(mesh, field_io.ELMER_EM)[0] == "|E|"
    assert field_io.resolve_field_shorthand("e", mesh) == "electric field re"
    assert field_io.guess_source_from_names(mesh.point_data.keys()) == field_io.ELMER_EM


def test_elmer_thermal_newest_and_scale(tmp_path):
    run = tmp_path / "th_data"
    run.mkdir()
    g = _grid(scale=1e-6)
    g.point_data["temperature"] = np.linspace(300, 350, g.n_points)
    g.save(str(run / "thermal_results_t0001.vtu"))
    files = field_io.discover(str(run), field_io.ELMER_THERMAL)
    assert len(files) == 1
    mesh, _, _ = field_io.load(files[0])
    assert field_io.scale_to_um(field_io.ELMER_THERMAL, mesh.bounds) == 1e6
    assert field_io.pick_default(mesh, field_io.ELMER_THERMAL) == ("temperature", field_io.EMSTUDIO_CMAP, False)


def test_amplitude_convention():
    assert field_io.is_amplitude_array("|E|")
    assert field_io.is_amplitude_array("E_real")
    assert field_io.is_amplitude_array("magnetic flux density im")
    assert not field_io.is_amplitude_array("S")
    assert not field_io.is_amplitude_array("U_e")
    assert not field_io.is_amplitude_array("whatever")


def test_load_out_of_range_cycle(tmp_path):
    run = _palace_run(tmp_path)
    pvd = field_io.discover(str(run), field_io.PALACE)[0]
    with pytest.raises(ValueError):
        field_io.load(pvd, 5)


def test_openems_fd_abs_arg_pair(tmp_path):
    sub = tmp_path / "run_data" / "sub-1"
    sub.mkdir(parents=True)
    g = pv.RectilinearGrid(np.linspace(0, 1e-4, 4), np.linspace(0, 1e-4, 3), np.linspace(0, 1e-5, 2))
    amp = np.tile([3.0, 4.0, 0.0], (g.n_points, 1))
    ph = np.tile([0.0, np.pi / 2, 0.0], (g.n_points, 1))
    g.point_data["E-Field"] = amp
    g.save(str(sub / "Ef_f=10000000000_abs.vtr"))
    g.point_data["E-Field"] = ph
    g.save(str(sub / "Ef_f=10000000000_arg.vtr"))
    g.save(str(sub / "Ef_f=10000000000_p=000.vtr"))

    files = field_io.discover(str(tmp_path / "run_data"), field_io.OPENEMS)
    assert [os.path.basename(f) for f in files] == ["Ef_f=10000000000_abs.vtr"]
    assert field_io.build_file_labels(files, field_io.OPENEMS)[files[0]] == "Ef - 10 GHz"
    mesh, _, _ = field_io.load(files[0])
    assert "E-Field" not in mesh.point_data
    assert np.allclose(mesh["E-Field re"][0], [3.0, 0.0, 0.0], atol=1e-12)
    assert np.allclose(mesh["E-Field im"][0], [0.0, 4.0, 0.0], atol=1e-12)
    assert field_io.attach_derived(mesh) == ["|E|"]
    assert np.isclose(mesh["|E|"][0], 5.0)
    assert field_io.scale_to_um(field_io.OPENEMS) == 1e6
    # _arg / phase snapshots map to the _abs file and its run folder.
    arg = str(sub / "Ef_f=10000000000_arg.vtr")
    snap = str(sub / "Ef_f=10000000000_p=000.vtr")
    assert field_io.openems_abs_path(arg) == files[0]
    assert field_io.openems_abs_path(snap) == files[0]
    assert field_io.openems_run_dir(arg) == str(tmp_path / "run_data")
    mesh2, _, _ = field_io.load(arg)
    assert "E-Field re" in mesh2.point_data
    assert field_io.guess_source_from_names(["E-Field re"]) == field_io.OPENEMS


def test_emstudio_colormap_ends():
    rgb = field_io.emstudio_colormap_rgb(np.array([0.0, 0.5, 1.0]))
    assert rgb.shape == (3, 3)
    assert rgb[0, 2] > 0.5 and rgb[0, 0] == 0.0  # low end: blue
    assert rgb[-1, 0] >= 0.5 and rgb[-1, 2] == 0.0  # high end: dark red
