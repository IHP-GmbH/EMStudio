# Palace examples (from Volker Muehlhaus / gds2palace)

Source: https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2

Each subfolder is a self-contained run: Python model + GDS + stackup XML.

| Folder | What it is |
|--------|------------|
| `L_2n0` | Classic 2 nH two-port inductor |
| `line_viaport` | Simple line with via port |
| `inductor_500pH` | 500 pH inductor, 2-port |
| `resistors_rsil` | RSIL resistors + derived layers (schema 3.1) |

## Run in EMStudio

1. Preferences: set `PALACE_PYTHON` (WSL venv with gds2palace≥0.4.1).
2. File → Open Python Model… → pick e.g. `L_2n0/palace_L2n0.py`.
3. Check GDS / XML paths on the Main tab (scripts usually expect files next to the .py).
4. Simulate → Run.

These files are local downloads (`examples/` is gitignored).
