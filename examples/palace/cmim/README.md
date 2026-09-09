# Palace CMIM example (SG13G2)

Small MIM capacitor from IHP PCell `cmim` with **w = l = 2.3 µm**
(geometry from [IHP-Open-PDK#493](https://github.com/IHP-GmbH/IHP-Open-PDK/issues/493)).

Ports like Volker
[`palace_rfcmim.py`](https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2/blob/main/workflow/palace_rfcmim.py).
Stackup is the **Palace** file
[`SG13G2_200um.xml`](https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2/blob/main/workflow/SG13G2_200um.xml)
(MIM gap ≈ **24.3 nm**, background SiO2). The older openEMS-style equivalent
(150 nm / εᵣ=26.8) is kept as `SG13G2_200um_openems_mim.xml` for comparison.

| File | Role |
|------|------|
| `cmim_orig.gds` | Source (hierarchical PCell instance) |
| `cmim_2u3_flat.gds` | Flattened DUT + feeds + ports; TM1 landing **1.26 µm** (TM1.a fail) |
| `cmim_2u3_tm1fix.gds` | Same, TM1 landing **1.64 µm** ([#493](https://github.com/IHP-GmbH/IHP-Open-PDK/issues/493) workaround) |
| `SG13G2_200um.xml` | Palace stackup (~24 nm MIM gap) |
| `SG13G2_200um_openems_mim.xml` | Previous equivalent-MIM stack (150 nm / εᵣ=26.8) |
| `palace_cmim.py` | Baseline model (1.26 µm TM1) |
| `palace_cmim_tm1fix.py` | Same ports/settings; uses `cmim_2u3_tm1fix.gds` |
| `_build_leads_gds.py` | Regenerates both GDS variants from `cmim_orig.gds` |

## Device

- MIM plate: 2.3 × 2.3 µm (unchanged in both GDS variants)
- Model C label: ~8.37 fF
- Metal1 leads (rfcmim-style reference)
- **A/B:** enlarge only TopMetal1 to ≥1.64 µm

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

## Model script (paths)

GDS and XML are **relative to the script directory**:

```python
gds_filename = "cmim_2u3_flat.gds"       # or "cmim_2u3_tm1fix.gds"
XML_filename = "SG13G2_200um.xml"
settings['margin'] = 20
settings['air_around'] = 20
settings['refined_cellsize'] = 0.15
settings['no_gui'] = True
```

EMStudio may rewrite paths to absolute when you open the file; that is fine locally.
Do not commit machine-specific absolute paths.

To compare against the old equivalent MIM, point `XML_filename` at
`SG13G2_200um_openems_mim.xml`.

## Run in EMStudio

1. Preferences → Palace:
   - `PALACE_PYTHON` — WSL/Linux venv with `gds2palace` (e.g. `/home/<user>/.venv/bin/python3`)
   - `PALACE_RUN_MODE=Executable`
   - `PALACE_INSTALL_PATH` — Palace prefix (e.g. `/home/<user>/palace-install`)
2. Open `palace_cmim.py` (or `palace_cmim_tm1fix.py`).
3. Simulate → Run.

**Note:** the ~24 nm MIM gap is thin; if meshing fails, fall back to
`SG13G2_200um_openems_mim.xml` or refine `refined_cellsize`.
