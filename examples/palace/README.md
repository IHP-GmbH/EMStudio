# Palace examples (from Volker Muehlhaus / gds2palace)

Source: https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2

Each subfolder is a self-contained run: Python model + GDS + stackup XML.

| Folder | What it is |
|--------|------------|
| `L_2n0` | Classic 2 nH two-port inductor |
| `line_viaport` | Simple line with via port |
| `inductor_500pH` | 500 pH inductor, 2-port |
| `balun_mim_vias` | 17–22 GHz MIM balun with dense via arrays (~3k vias, merge=3 µm) |
| `core_1t10f_w_incaps_ports_simplified` | Multi-port RF core with in-caps (simplified GDS), 0–50 GHz |
| `palace_core_dev` | 50 GHz MPA core (no BJT), 1–350 GHz |
| `resistors_rsil` | RSIL resistors + derived layers (schema 3.1) |
| `cmim` | Small CMIM 2.3 µm (IHP#493 geometry), 0–100 GHz |

## Run in EMStudio

1. Preferences: set `PALACE_PYTHON` (WSL venv with gds2palace≥0.4.1).
2. File → Open Python Model… → pick e.g. `L_2n0/palace_L2n0.py`.
3. Check GDS / XML paths on the Main tab (scripts usually expect files next to the .py).
4. Simulate → Run.

After a run with field dumps enabled (`settings['fdump']` / VTK output), open the **Substrate** tab and click **Field** on the layout preview to see a Z-clip heatmap. Needs Preferences **FIELD_VIEWER_PYTHON** with `pyvista` + `pillow`.

These files are local downloads (`examples/` is gitignored).
