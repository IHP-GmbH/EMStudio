#!/usr/bin/env python3
# -*- coding: utf-8 -*-
########################################################################
#
# Copyright 2025-2026 Volker Muehlhaus and IHP PDK Authors
#
# Licensed under the GNU General Public License, Version 3.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.gnu.org/licenses/gpl-3.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
########################################################################
"""Field-result discovery and reading shared by EMStudio's field tools.

Used by the 3D viewer (field_viewer.py) and the Layout 2D slice exporter
(field_slice_export.py), so both find the same files, read the same cycle,
use the same units and pick the same default field. No Qt dependency.

Implements FIELD_VIEWER_SPEC.md sections 2 and 3:

- discovery of candidate files per source (.pvd > .pvtu > .vtu, never
  partition pieces when an index exists, AMR iteration copies flagged),
- cycle-aware reading of .pvd collections (plain pv.read() on a .pvd only
  ever returns the first dataset),
- cycle / file labels with the solved frequency,
- native-unit -> um scale per source,
- array classification, complex-pair detection, derived magnitudes and
  user-oriented display names.

Command line (used by EMStudio to fill its file/cycle picker):

    python field_io.py --list --run-path <..._data> --source palace

prints one JSON object on stdout.

Sources are registered in SOURCES. Adding a solver (e.g. openEMS
frequency-domain dumps) means adding a SourcePreset with a discovery
function and, if its array names differ, entries in _QUANTITY_ALIASES.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Callable, Optional

PALACE = "palace"
ELMER_EM = "elmer_em"
ELMER_THERMAL = "elmer_thermal"
OPENEMS = "openems"

_ITERATION_RE = re.compile(r"^iteration(\d+)$")
_ELMER_STEP_RE = re.compile(r"_t(\d+)\.p?vtu$", re.IGNORECASE)

# Arrays that are solver bookkeeping, not fields, plus helpers the viewers add.
HIDDEN_ARRAYS = {
    "attribute", "geometryids", "rank", "vtkoriginalpointids",
    "vtkoriginalcellids", "vtkghosttype", "_glyph_arrow_length",
}


# ---------------------------------------------------------------------------
# Source registry
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class SourcePreset:
    """Per-source defaults (spec 3.2 and 4.6)."""
    source_id: str
    title: str
    scale_to_um: Optional[float]  # None = guess from bounds
    colormap: str
    log_scale: bool
    discover: Callable[[str], list]
    cycle_is_frequency_ghz: bool = False  # .pvd timestep is f in GHz (Palace)
    cycle_is_step: bool = False  # .pvd timestep is frequencies.dat row (Elmer EM)
    no_files_hint: str = ""
    extensions: tuple = field(default=(".pvd", ".pvtu", ".vtu"))


# EMStudio field color scale, shared by the Layout 2D slice and the 3D viewer
# so both read the same: dark blue -> blue -> cyan -> yellow -> red -> dark red.
EMSTUDIO_CMAP = "emstudio"


def emstudio_colormap_rgb(t):
    """RGB in [0, 1] (shape (..., 3)) for normalized values t in [0, 1]."""
    import numpy as np

    t = np.clip(np.asarray(t, dtype=float), 0.0, 1.0)
    return np.stack([np.clip(1.5 - np.abs(3.5 * t - k), 0.0, 1.0) for k in (2.5, 1.5, 0.5)],
                    axis=-1)


def _glob_sorted(pattern: str, recursive: bool = False) -> list:
    return sorted(glob.glob(pattern, recursive=recursive))


def _by_precedence(directory: str, stem: str = "*", recursive: bool = False) -> list:
    """Spec 2.1: first of .pvd / .pvtu / .vtu that matches in one location."""
    if not directory or not os.path.isdir(directory):
        return []
    sub = "**" if recursive else ""
    for ext in (".pvd", ".pvtu", ".vtu"):
        hits = _glob_sorted(os.path.join(directory, sub, stem + ext), recursive=recursive)
        if hits:
            return hits
    return []


def is_amr_iteration_path(path: str) -> bool:
    """True if any path component is an AMR ``iteration<N>`` folder."""
    return any(_ITERATION_RE.match(p) for p in os.path.normpath(path).split(os.sep))


def model_basename_from_run_dir(run_dir: str) -> str:
    base = os.path.basename(os.path.normpath(run_dir))
    return base[:-5] if base.endswith("_data") else base


def palace_output_dir(run_dir: str) -> str:
    """Problem.Output from config.json, else output/<model_basename>."""
    output_rel = None
    try:
        with open(os.path.join(run_dir, "config.json"), "r", encoding="utf-8") as fh:
            output_rel = json.load(fh).get("Problem", {}).get("Output")
    except (OSError, ValueError, AttributeError):
        output_rel = None
    if not output_rel:
        output_rel = "output/" + model_basename_from_run_dir(run_dir)
    return os.path.normpath(os.path.join(run_dir, output_rel))


def discover_palace(run_dir: str) -> list:
    hits = _by_precedence(palace_output_dir(run_dir), recursive=True)
    if hits:
        return hits
    # EMStudio may hand us palace_model/ or a non-standard layout: look for
    # Palace's own paraview/ folders anywhere below.
    for ext in (".pvd", ".pvtu", ".vtu"):
        hits = [p for p in _glob_sorted(os.path.join(run_dir, "**", "*" + ext), recursive=True)
                if "paraview" in os.path.normpath(p).split(os.sep)]
        if hits:
            return hits
    return []


def discover_elmer_em(run_dir: str) -> list:
    for d in (os.path.join(run_dir, "mesh"), run_dir):
        hits = _by_precedence(d, stem="fields*")
        if hits:
            return hits
    return []


def discover_elmer_thermal(run_dir: str) -> list:
    run_dir = os.path.normpath(run_dir)
    dirs = [run_dir, os.path.dirname(run_dir)]
    for ext in (".pvtu", ".vtu"):
        hits = []
        for d in dirs:
            hits += glob.glob(os.path.join(d, "thermal_results*" + ext))
        if hits:
            return [max(hits, key=os.path.getmtime)]
    return []


_OPENEMS_FD_RE = re.compile(r"^(?P<name>.+)_f=(?P<freq>[0-9.eE+-]+)_abs\.vt[ru]$")


def discover_openems(run_dir: str) -> list:
    """openEMS frequency-domain VTK dumps (DumpType 10/11, FileType 0).

    openEMS writes per dump box and frequency ``<name>_f=<Hz>_abs.vtr``
    (per-component amplitude), ``..._arg.vtr`` (phase, rad) and phase
    snapshots ``..._p=<deg>.vtr``. One candidate per ``_abs`` file; the
    reader combines it with its ``_arg`` sibling. Excitations live in
    ``sub-<N>/`` folders.
    """
    hits = [p for p in _glob_sorted(os.path.join(run_dir, "**", "*_abs.vt[ru]"), recursive=True)
            if _OPENEMS_FD_RE.match(os.path.basename(p))]
    return hits


_OPENEMS_AUX_RE = re.compile(r"^(?P<stem>.+_f=[0-9.eE+-]+)_(?:arg|p=\d+)(?P<ext>\.vt[ru])$")


def openems_abs_path(path: str) -> str:
    """The ``_abs`` file for any openEMS FD dump file (``_arg`` / ``_p=<deg>``),
    if it exists; otherwise ``path`` unchanged."""
    m = _OPENEMS_AUX_RE.match(os.path.basename(path))
    if m is None:
        return path
    cand = os.path.join(os.path.dirname(path), f"{m.group('stem')}_abs{m.group('ext')}")
    return cand if os.path.isfile(cand) else path


def openems_run_dir(path: str) -> Optional[str]:
    """Run folder (``..._data``) of an openEMS FD dump file, for offering its
    other frequencies / excitations; None if ``path`` is no such file."""
    if openems_fd_parts(openems_abs_path(path)) is None:
        return None
    d = os.path.dirname(os.path.abspath(path))
    if re.match(r"^sub-\d+$", os.path.basename(d)):
        d = os.path.dirname(d)
    return d


def openems_fd_parts(path: str):
    """(dump_name, freq_Hz, arg_path or None) for an openEMS ``_abs`` file, else None."""
    m = _OPENEMS_FD_RE.match(os.path.basename(path))
    if m is None:
        return None
    try:
        freq = float(m.group("freq"))
    except ValueError:
        freq = None
    arg = re.sub(r"_abs(\.vt[ru])$", r"_arg\1", path)
    return m.group("name"), freq, (arg if os.path.isfile(arg) else None)


SOURCES = {
    PALACE: SourcePreset(
        PALACE, "Palace", 1.0, EMSTUDIO_CMAP, True, discover_palace,
        cycle_is_frequency_ghz=True,
        no_files_hint="No Palace field dump found. Set fdump to the frequencies to "
                      "save before running."),
    ELMER_EM: SourcePreset(
        ELMER_EM, "Elmer EM", 1.0, EMSTUDIO_CMAP, True, discover_elmer_em,
        cycle_is_step=True,
        no_files_hint="No Elmer field dump found. Enable field dump (fdump) before running."),
    ELMER_THERMAL: SourcePreset(
        ELMER_THERMAL, "Elmer Thermal", 1e6, EMSTUDIO_CMAP, False, discover_elmer_thermal,
        no_files_hint="No thermal_results*.vtu found. Run the thermal simulation first."),
    # openEMS VTK dumps are written in SI meters.
    OPENEMS: SourcePreset(
        OPENEMS, "openEMS", 1e6, EMSTUDIO_CMAP, True, discover_openems,
        no_files_hint="No openEMS frequency-domain field dump (*_abs.vtr) found. Add an "
                      "E-field FD dump box (DumpType 10) before running.",
        extensions=(".vtr",)),
}


def preset(source: Optional[str]) -> Optional[SourcePreset]:
    return SOURCES.get(source or "")


def discover(run_dir: str, source: str) -> list:
    """All candidate files for this run, sorted; [] on any problem."""
    p = preset(source)
    if p is None or not run_dir or not os.path.isdir(run_dir):
        return []
    try:
        return [os.path.abspath(x) for x in p.discover(run_dir)]
    except OSError:
        return []


def guess_source_from_names(names) -> Optional[str]:
    """Best guess of the source for a file opened without one (spec 2.4 note)."""
    low = [n.lower() for n in names]
    if any(n.startswith("electric field") for n in low):
        return ELMER_EM
    if any(n.startswith(("e-field", "h-field")) for n in low):
        return OPENEMS
    if any(n in ("e_real", "e_imag", "u_e") for n in low):
        return PALACE
    if any("temp" in n for n in low):
        return ELMER_THERMAL
    return None


def guess_scale_to_um(bounds) -> float:
    """Heuristic native->um factor from a bounding box (legacy fallback)."""
    span = max(abs(bounds[1] - bounds[0]), abs(bounds[3] - bounds[2]), abs(bounds[5] - bounds[4]))
    if span <= 0 or span < 1e-2:
        return 1.0e6
    if span > 1e4:
        return 1.0e-3
    return 1.0


def scale_to_um(source: Optional[str], bounds=None) -> float:
    p = preset(source)
    if p is not None and p.scale_to_um is not None:
        return float(p.scale_to_um)
    return guess_scale_to_um(bounds) if bounds is not None else 1.0


# ---------------------------------------------------------------------------
# Frequencies, cycles and labels
# ---------------------------------------------------------------------------

def read_frequencies_dat(start_dir: str) -> dict:
    """{step: frequency_Hz} from frequencies.dat next to start_dir or one up."""
    for d in (start_dir, os.path.dirname(os.path.normpath(start_dir))):
        path = os.path.join(d, "frequencies.dat")
        if not os.path.isfile(path):
            continue
        out = {}
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    parts = line.split()
                    if len(parts) < 2:
                        continue
                    try:
                        out[int(float(parts[0]))] = float(parts[1])
                    except ValueError:
                        continue
        except OSError:
            continue
        return out
    return {}


def format_ghz(freq_hz: float) -> str:
    return f"{freq_hz / 1e9:g} GHz"


def elmer_step_of(path: str) -> Optional[int]:
    m = _ELMER_STEP_RE.search(os.path.basename(path))
    return int(m.group(1)) if m else None


def build_file_labels(paths, source: Optional[str] = None) -> dict:
    """{path: label} per spec 4.1: relative to the common ancestor, trailing
    ``<dir>/<dir>.<ext>`` collapsed, volume/boundary icons when mixed, and
    the Elmer EM step frequency appended."""
    paths = list(paths)
    if not paths:
        return {}
    labels = {}
    if len(paths) == 1:
        labels[paths[0]] = os.path.basename(paths[0])
    else:
        root = os.path.commonpath(paths)
        for path in paths:
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            if "/" in rel:
                dir_part, filename = rel.rsplit("/", 1)
                stem = os.path.splitext(filename)[0]
                last_dir = dir_part.rsplit("/", 1)[-1]
                if last_dir == stem:
                    parent = dir_part.rsplit("/", 1)[0] if "/" in dir_part else ""
                    rel = f"{parent}/{filename}" if parent else filename
            labels[path] = rel
        is_boundary = {p: "boundary" in p.lower() for p in paths}
        if len(set(is_boundary.values())) > 1:
            for p in paths:
                icon = "\U0001F532" if is_boundary[p] else "\U0001F9CA"
                labels[p] = f"{icon} {labels[p]}"

    if source == OPENEMS:
        for p in paths:
            parts = openems_fd_parts(p)
            if parts is None:
                continue
            name, freq, _arg = parts
            head = labels[p].rsplit("/", 1)[0] + "/" if "/" in labels[p] else ""
            labels[p] = f"{head}{name}" + (f" - {format_ghz(freq)}" if freq else "")
    if source == ELMER_EM:
        for p in paths:
            step = elmer_step_of(p)
            if step is None:
                continue
            freq = read_frequencies_dat(os.path.dirname(p)).get(step)
            if freq is not None:
                labels[p] = f"{labels[p]} - {format_ghz(freq)}"
    return labels


def _pvd_timesteps(path: str) -> list:
    """[(timestep_float_or_None, [dataset files])] in file order, one entry per
    distinct timestep (a timestep may have several parts)."""
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return []
    order = []
    groups = {}
    base = os.path.dirname(path)
    for ds in root.iter("DataSet"):
        ts_raw = ds.get("timestep")
        try:
            ts = float(ts_raw) if ts_raw is not None else None
        except ValueError:
            ts = None
        key = ts_raw if ts_raw is not None else f"#{len(order)}"
        if key not in groups:
            groups[key] = (ts, [])
            order.append(key)
        f = ds.get("file")
        if f:
            groups[key][1].append(os.path.join(base, f))
    # VTK's PVD reader sorts by time value.
    entries = [groups[k] for k in order]
    if all(e[0] is not None for e in entries):
        entries.sort(key=lambda e: e[0])
    return entries


def _xml_header(path: str, limit: int = 1 << 20) -> Optional[str]:
    """Text of a VTK XML file up to its binary appended section, or None if
    the header isn't within ``limit`` bytes (e.g. inline base64 data)."""
    try:
        with open(path, "rb") as fh:
            chunk = fh.read(limit)
    except OSError:
        return None
    cut = chunk.find(b"<AppendedData")
    if cut >= 0:
        chunk = chunk[:cut]
    elif len(chunk) >= limit:
        return None
    return chunk.decode("utf-8", errors="replace")


def _file_has_point_data(path: str) -> Optional[bool]:
    """True/False from the XML header only; None when it can't be told cheaply."""
    text = _xml_header(path)
    if text is None:
        return None
    tag = "PPointData" if path.lower().endswith(".pvtu") else "PointData"
    m = re.search(r"<%s\b[^>]*?(/>|>(.*?)</%s>)" % (tag, tag), text, re.S)
    if m is None:
        # Tag absent altogether: the whole header is here, so no point data.
        return False if (tag == "PPointData" or "</Piece>" in text) else None
    body = m.group(2) or ""
    return "DataArray" in body


def _cycle_has_point_data(files) -> Optional[bool]:
    states = [_file_has_point_data(f) for f in files] or [None]
    if any(s is True for s in states):
        return True
    if all(s is False for s in states):
        return False
    return None


def cycle_count(path: str) -> int:
    if path.lower().endswith(".pvd"):
        return max(len(_pvd_timesteps(path)), 1)
    return 1


def cycle_infos(path: str, source: Optional[str]) -> list:
    """[{label, timestep, geometry}] per cycle (spec 3.1). ``geometry`` is
    True/False when known from headers, None when unknown until visited."""
    if not path.lower().endswith(".pvd"):
        return [{"label": "Cycle 1", "timestep": None, "geometry": None}]
    p = preset(source)
    freqs = read_frequencies_dat(os.path.dirname(path)) if p and p.cycle_is_step else {}
    infos = []
    for i, (ts, files) in enumerate(_pvd_timesteps(path)):
        has_pd = _cycle_has_point_data(files)
        geometry = None if has_pd is None else (not has_pd)
        n = i + 1
        label = f"Cycle {n}"
        if geometry:
            label = f"geometry (cycle {n})"
        elif ts is not None and p is not None:
            if p.cycle_is_frequency_ghz:
                label = f"{ts:g} GHz (cycle {n})"
            elif p.cycle_is_step and int(ts) in freqs:
                label = f"{format_ghz(freqs[int(ts)])} (cycle {n})"
        infos.append({"label": label, "timestep": ts, "geometry": geometry})
    return infos or [{"label": "Cycle 1", "timestep": None, "geometry": None}]


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------

def _pick_block(data):
    import pyvista as pv

    if not isinstance(data, pv.MultiBlock):
        return data
    blocks = [b for b in data if b is not None]
    for b in reversed(blocks):
        if isinstance(b, pv.MultiBlock):
            b = _pick_block(b)
        if b is not None and getattr(b, "n_points", 0) > 0:
            return b
    if blocks:
        return blocks[-1]
    raise ValueError("no readable block")


def load(path: str, cycle_index: Optional[int] = None):
    """Read one cycle of ``path`` -> (mesh, cycle_index, num_cycles).

    .pvd uses the reader's time API; .pvtu/.vtu/.vtr have one cycle.
    Raises ValueError for an out-of-range cycle or an empty file.
    """
    import pyvista as pv

    if not os.path.isfile(path):
        raise ValueError(f"file not found: {path}")
    path = openems_abs_path(path)
    if path.lower().endswith(".pvd"):
        reader = pv.get_reader(path)
        times = list(reader.time_values)
        if not times:
            raise ValueError(f"no cycles found in {path}")
        if cycle_index is None:
            cycle_index = 0
        if not 0 <= cycle_index < len(times):
            raise ValueError(f"cycle {cycle_index + 1} out of range for {path} "
                             f"(has {len(times)} cycle(s))")
        reader.set_active_time_value(times[cycle_index])
        return _pick_block(reader.read()), cycle_index, len(times)
    if cycle_index not in (None, 0):
        raise ValueError(f"cycle {cycle_index + 1} out of range for {path} (has 1 cycle)")
    mesh = _pick_block(pv.read(path))
    parts = openems_fd_parts(path)
    if parts is not None and parts[2] is not None:
        mesh = _openems_combine_abs_arg(mesh, _pick_block(pv.read(parts[2])))
    return mesh, 0, 1


def _openems_combine_abs_arg(abs_mesh, arg_mesh):
    """Replace openEMS per-component amplitude/phase arrays by ``<name> re`` /
    ``<name> im`` (phasor = abs * exp(j*arg)), so the generic complex-pair
    logic derives |E| and offers the real/imaginary parts."""
    import numpy as np

    out = abs_mesh.copy()
    for name in list(abs_mesh.point_data.keys()):
        if name not in arg_mesh.point_data:
            continue
        amp = np.asarray(abs_mesh.point_data[name], dtype=float)
        ph = np.asarray(arg_mesh.point_data[name], dtype=float)
        if amp.shape != ph.shape:
            continue
        out.point_data[f"{name} re"] = amp * np.cos(ph)
        out.point_data[f"{name} im"] = amp * np.sin(ph)
        del out.point_data[name]
    return out


# ---------------------------------------------------------------------------
# Arrays: classification, complex pairs, derived magnitudes, display names
# ---------------------------------------------------------------------------

# Canonical symbol -> (quantity, unit, amplitude-like for dB, sort rank).
_QUANTITIES = {
    "E": ("electric field", "V/m", True, 0),
    "B": ("magnetic flux density", "T", True, 1),
    "H": ("magnetic field strength", "A/m", True, 2),
    "J_s": ("surface current density", "A/m", True, 3),
    "Q_s": ("surface charge density", "C/m²", False, 4),
    "S": ("Poynting vector", "W/m²", False, 5),
    "U_e": ("electric energy density", "J/m³", False, 6),
    "U_m": ("magnetic energy density", "J/m³", False, 7),
    "temperature": ("temperature", "", False, 8),
    "D": ("electric flux density", "C/m²", True, 9),
    "J": ("current density", "A/m²", True, 10),
}

# Solver-specific base names -> canonical symbol (lower-case keys).
_QUANTITY_ALIASES = {
    "e": "E", "electric field": "E",
    "b": "B", "magnetic flux density": "B",
    "h": "H", "magnetic field strength": "H",
    "j_s": "J_s", "q_s": "Q_s",
    "s": "S", "u_e": "U_e", "u_m": "U_m",
    "temperature": "temperature", "temp": "temperature",
    # openEMS dump array names
    "e-field": "E", "h-field": "H", "d-field": "D", "b-field": "B", "j-field": "J",
}

_COMPLEX_SUFFIXES = (("_real", "_imag"), (" re", " im"), ("_re", "_im"))

GROUP_MAGNITUDE, GROUP_COMPLEX, GROUP_VECTOR, GROUP_SCALAR, GROUP_OTHER = range(5)


def canonical_symbol(base: str) -> Optional[str]:
    return _QUANTITY_ALIASES.get(base.strip().lower())


def visible_point_arrays(mesh) -> list:
    return [n for n in mesh.point_data.keys() if n.lower() not in HIDDEN_ARRAYS]


def complex_pairs(names) -> list:
    """[(base, real_name, imag_name)] found by name pattern (spec 3.3)."""
    names = list(names)
    present = set(names)
    pairs = []
    used = set()
    for n in names:
        for re_suf, im_suf in _COMPLEX_SUFFIXES:
            if n.endswith(re_suf) and len(n) > len(re_suf):
                base = n[: -len(re_suf)]
                imag = base + im_suf
                if imag in present and n not in used:
                    pairs.append((base, n, imag))
                    used.update((n, imag))
                break
    return pairs


def magnitude_name(base: str) -> str:
    sym = canonical_symbol(base)
    return f"|{sym or base}|"


def attach_derived(mesh, source: Optional[str] = None) -> list:
    """Add |X| for every complex pair (per-component |re + j im|, then the
    vector norm). Returns the names added. Missing/odd arrays are skipped."""
    import numpy as np

    added = []
    for base, re_name, im_name in complex_pairs(visible_point_arrays(mesh)):
        try:
            re_arr = np.asarray(mesh.point_data[re_name], dtype=float)
            im_arr = np.asarray(mesh.point_data[im_name], dtype=float)
            if re_arr.shape != im_arr.shape:
                continue
            per = np.sqrt(re_arr ** 2 + im_arr ** 2)
            mag = np.linalg.norm(per, axis=1) if per.ndim > 1 else per
        except Exception:
            continue
        name = magnitude_name(base)
        if name in mesh.point_data and name not in added:
            continue
        mesh.point_data[name] = mag
        added.append(name)
    return added


@dataclass
class FieldEntry:
    name: str  # raw array name (point data key)
    display: str
    tooltip: str
    group: int
    is_vector: bool
    symbol: Optional[str] = None
    rank: int = 99
    unit: str = ""

    @property
    def title(self) -> str:
        """Short legend title with unit. VTK's legend font drops "|", so a
        derived magnitude is spelled out ("E magnitude") to not read as E."""
        name = self.name
        if self.group == GROUP_MAGNITUDE and name.startswith("|") and name.endswith("|"):
            name = f"{name[1:-1]} magnitude"
        return f"{name} [{self.unit}]" if self.unit else name


def _unit_suffix(unit: str) -> str:
    return f" [{unit}]" if unit else ""


def field_entries(mesh) -> list:
    """User-oriented, grouped field list (spec 4.6 Recommended).

    Order: derived magnitudes, complex parts, other vectors, known scalars,
    unknown arrays under their raw names. Nothing is dropped.
    """
    names = visible_point_arrays(mesh)
    parts = {}
    for base, re_name, im_name in complex_pairs(names):
        parts[re_name] = (base, "real part")
        parts[im_name] = (base, "imaginary part")

    entries = []
    for n in names:
        arr = mesh.point_data[n]
        is_vec = getattr(arr, "ndim", 1) > 1 and arr.shape[1] > 1
        tip = f"Array: {n}" + (" (vector)" if is_vec else "")
        if n.startswith("|") and n.endswith("|") and len(n) > 2:
            sym = n[1:-1]
            q = _QUANTITIES.get(sym)
            desc = q[0] if q else sym
            unit = q[1] if q else ""
            entries.append(FieldEntry(
                n, f"{n} – {desc} magnitude{_unit_suffix(unit)}",
                f"Derived: peak magnitude of the complex phasor\n{tip}",
                GROUP_MAGNITUDE, False, sym, q[3] if q else 50, unit))
            continue
        if n in parts:
            base, which = parts[n]
            sym = canonical_symbol(base)
            q = _QUANTITIES.get(sym or "")
            short = "Re" if which == "real part" else "Im"
            if q:
                disp = f"{sym} {short} – {q[0]}, {which}{_unit_suffix(q[1])}"
                rank = q[3]
            else:
                disp = n
                rank = 50
            entries.append(FieldEntry(n, disp, tip, GROUP_COMPLEX, is_vec, sym,
                                      rank * 2 + (0 if short == "Re" else 1),
                                      q[1] if q else ""))
            continue
        sym = canonical_symbol(n)
        if sym is None and "temp" in n.lower():
            sym = "temperature"
        q = _QUANTITIES.get(sym or "")
        if q:
            disp = n if n.lower() == q[0] else f"{n} – {q[0]}"
            entries.append(FieldEntry(
                n, disp + _unit_suffix(q[1]), tip,
                GROUP_VECTOR if is_vec else GROUP_SCALAR, is_vec, sym, q[3], q[1]))
        else:
            entries.append(FieldEntry(n, n, tip, GROUP_OTHER, is_vec, None, 99))

    order = {n: i for i, n in enumerate(names)}
    entries.sort(key=lambda e: (e.group, e.rank, order[e.name]))
    return entries


def is_amplitude_array(name: str) -> bool:
    """True if a dB range uses 20*log10 (field amplitude), else 10*log10."""
    if name.startswith("|") and name.endswith("|"):
        sym = name[1:-1]
    else:
        sym = canonical_symbol(name)
        for suf in (s for pair in _COMPLEX_SUFFIXES for s in pair):
            if sym is None and name.endswith(suf):
                sym = canonical_symbol(name[: -len(suf)])
    q = _QUANTITIES.get(sym or "")
    return bool(q and q[2])


def array_magnitudes(values):
    import numpy as np

    values = np.asarray(values)
    return np.linalg.norm(values, axis=1) if values.ndim > 1 else values


def db_range_to_clim(mesh, name: str, db_range: float):
    """(floor, max) spanning db_range dB below the array's maximum."""
    mags = array_magnitudes(mesh[name])
    pos = mags[mags > 0]
    if pos.size == 0:
        return None
    top = float(pos.max())
    per_decade = 20.0 if is_amplitude_array(name) else 10.0
    return top / 10.0 ** (db_range / per_decade), top


def pick_default(mesh, source: Optional[str]):
    """(array_name or None, colormap, log_scale) for the initial view."""
    available = visible_point_arrays(mesh)
    if source in (PALACE, ELMER_EM, OPENEMS) and "|E|" in available:
        p = SOURCES[source]
        return "|E|", p.colormap, p.log_scale
    if source == ELMER_THERMAL:
        key = next((k for k in available if "temp" in k.lower()), None)
        if key is not None:
            return key, SOURCES[ELMER_THERMAL].colormap, False
    if not available:
        return None, EMSTUDIO_CMAP, False
    entries = field_entries(mesh)
    return (entries[0].name if entries else available[0]), EMSTUDIO_CMAP, False


def resolve_array_name(mesh, name: str) -> Optional[str]:
    """Exact array name, or a legacy alias (``E_magnitude`` -> ``|E|``)."""
    if name in mesh.point_data:
        return name
    if name.lower() == "e_magnitude" and "|E|" in mesh.point_data:
        return "|E|"
    return None


def resolve_field_shorthand(shorthand: str, mesh) -> Optional[str]:
    """CLI --field e|b|s|temp -> array name present in mesh, or None."""
    names = visible_point_arrays(mesh)
    if shorthand == "temp":
        return next((k for k in names if "temp" in k.lower()), None)
    want = {"e": "E", "b": "B", "s": "S"}.get(shorthand)
    if want is None:
        return None
    if want == "S":
        return next((k for k in names if canonical_symbol(k) == "S"), None)
    for base, re_name, _im in complex_pairs(names):
        if canonical_symbol(base) == want:
            return re_name
    return next((k for k in names if canonical_symbol(k) == want), None)


# ---------------------------------------------------------------------------
# Run listing (JSON for EMStudio)
# ---------------------------------------------------------------------------

def list_run(run_dir: str, source: str, files=None) -> dict:
    paths = list(files) if files else discover(run_dir, source)
    labels = build_file_labels(paths, source)
    p = preset(source)
    out = {
        "source": source,
        "run_dir": os.path.abspath(run_dir) if run_dir else "",
        "scale_to_um": p.scale_to_um if p else None,
        "hint": "" if paths else (p.no_files_hint if p else "Unknown source."),
        "files": [],
    }
    for path in paths:
        out["files"].append({
            "path": path,
            "label": labels.get(path, os.path.basename(path)),
            "amr": is_amr_iteration_path(path),
            "boundary": "boundary" in path.lower(),
            "cycles": cycle_infos(path, source),
        })
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="EMStudio field-result discovery")
    ap.add_argument("--list", action="store_true", help="print candidate files as JSON")
    ap.add_argument("--run-path", default="", help="a *_data run directory")
    ap.add_argument("--source", default=PALACE, choices=sorted(SOURCES))
    ap.add_argument("files", nargs="*", help="explicit files instead of discovery")
    args = ap.parse_args(argv)
    if not args.list:
        ap.error("nothing to do (use --list)")
    data = list_run(args.run_path, args.source, args.files or None)
    sys.stdout.write(json.dumps(data, ensure_ascii=False) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
