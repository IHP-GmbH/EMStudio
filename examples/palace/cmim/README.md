# Palace CMIM example (SG13G2)

Small MIM capacitor from IHP PCell `cmim` with **w = l = 2.3 µm**
(geometry from [IHP-Open-PDK#493](https://github.com/IHP-GmbH/IHP-Open-PDK/issues/493)).

Stackup and ports taken from Volker’s
[`palace_rfcmim.py`](https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2/blob/main/workflow/palace_rfcmim.py)
(+ [`SG13G2_200um.xml`](https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2/blob/main/workflow/SG13G2_200um.xml) as-is).

| File | Role |
|------|------|
| `cmim_orig.gds` | Source (hierarchical PCell instance) |
| `cmim_2u3_flat.gds` | Flattened DUT + feeds + ports; TM1 landing **1.26 µm** (TM1.a fail) |
| `cmim_2u3_tm1fix.gds` | Same, but TM1 landing **1.64 µm** ([#493](https://github.com/IHP-GmbH/IHP-Open-PDK/issues/493) workaround) |
| `SG13G2_200um.xml` | Volker `palace_rfcmim` stack **without** SUBGND / BACKSIDEGND / LBE |
| `palace_cmim.py` | Baseline model (1.26 µm TM1) |
| `palace_cmim_tm1fix.py` | Same ports/settings; uses `cmim_2u3_tm1fix.gds` |
| `_build_leads_gds.py` | Regenerates both GDS variants from `cmim_orig.gds` |
| `gds2palace/` | Junction to PDK Volker package (import fallback) |

## Device

- MIM plate: 2.3 × 2.3 µm (unchanged in both GDS variants)
- Model C label: ~8.37 fF
- Added Metal1 leads (rfcmim-style reference) — not in the bare PCell
- **A/B:** enlarge only TopMetal1 to ≥1.64 µm; expect intrinsic C almost unchanged

## Ports (palace_rfcmim)

| Port | Via | GDS layer |
|------|-----|-----------|
| 1 | Metal1 → TopMetal1 | 201 |
| 2 | Metal1 → Metal5 | 202 |

Zero-width vertical line footprints on the Metal1/TM1 and Metal1/M5 leads.

## Frequencies

- Sweep: **1 → 100 GHz**, step **2.5 GHz**
- Extra points: 1, 5, 10, 20, 50, 100 GHz
- Field dump: 10 GHz

## Run in EMStudio

1. Preferences: `PALACE_PYTHON`; `PALACE_RUN_MODE=Executable`; `PALACE_INSTALL_PATH=/home/adatsuk/palace-install`.
2. Open `palace_cmim.py`.
3. Simulate → Run.
