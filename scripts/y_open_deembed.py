#!/usr/bin/env python3
"""Open-fixture Y-parameter de-embed for 2-port Touchstone data.

  Y_dut = Y_meas - Y_open

Use the same mesh/settings for both runs. Typical CMIM flow:
  meas = palace_cmim.s2p          (DUT with MIM + Vmim)
  open = palace_cmim_open.s2p     (pads/feeders/frame only)

Prints series pi-model C at selected frequencies and writes
  <meas_stem>_yopen.sNp
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import skrf as rf


def interpolate_to(nw: rf.Network, freq: rf.Frequency) -> rf.Network:
    """Resample nw onto freq (Hz grid of the measurement)."""
    if np.allclose(nw.f, freq.f):
        return nw
    return nw.interpolate(freq)


def y_open_deembed(nw_meas: rf.Network, nw_open: rf.Network) -> rf.Network:
    if nw_meas.nports != nw_open.nports:
        raise ValueError(f"port count mismatch: meas={nw_meas.nports} open={nw_open.nports}")
    open_i = interpolate_to(nw_open, nw_meas.frequency)
    y = nw_meas.y - open_i.y
    out = rf.Network()
    out.frequency = nw_meas.frequency
    out.z0 = nw_meas.z0
    out.y = y
    out.name = (nw_meas.name or "meas") + "_yopen"
    return out


def c_pi_fF(nw: rf.Network, f_hz: float) -> tuple[float, float, float, float]:
    i = int(np.argmin(np.abs(nw.f - f_hz)))
    f = float(nw.f[i])
    Y = nw.y[i]
    w = 2.0 * np.pi * f
    cser = -np.imag(Y[0, 1]) / w * 1e15
    csh1 = (np.imag(Y[0, 0]) + np.imag(Y[0, 1])) / w * 1e15
    csh2 = (np.imag(Y[1, 1]) + np.imag(Y[0, 1])) / w * 1e15
    return f, cser, csh1, csh2


def report(label: str, nw: rf.Network, freqs_ghz=(1.0, 5.0, 10.0, 20.0)) -> None:
    print(f"\n=== {label} ===")
    for g in freqs_ghz:
        f, cser, csh1, csh2 = c_pi_fF(nw, g * 1e9)
        print(f"  {f/1e9:6.2f} GHz  Cser={cser:7.3f} fF  "
              f"Csh1={csh1:6.3f}  Csh2={csh2:6.3f}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("meas", type=Path, help="DUT Touchstone (.s2p)")
    ap.add_argument("open", type=Path, help="open-fixture Touchstone (.s2p)")
    ap.add_argument("-o", "--output", type=Path, default=None,
                    help="output Touchstone (default: <meas>_yopen.sNp)")
    args = ap.parse_args(argv)

    nw_m = rf.Network(str(args.meas))
    nw_o = rf.Network(str(args.open))
    nw_d = y_open_deembed(nw_m, nw_o)

    report("meas (raw)", nw_m)
    report("open", nw_o)
    report("Y_meas - Y_open", nw_d)

    out = args.output
    if out is None:
        stem = args.meas.with_suffix("")
        out = Path(f"{stem}_yopen.s{nw_m.nports}p")
    nw_d.write_touchstone(str(out.with_suffix("")), form="db",
                          skrf_comment="Y-parameter open de-embed: Y_meas - Y_open")
    # write_touchstone may append .sNp itself depending on path; normalize message
    written = out if out.exists() else Path(str(out.with_suffix("")) + f".s{nw_m.nports}p")
    print(f"\nWrote {written}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
