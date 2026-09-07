# Elmer examples (from Volker Muehlhaus / gds2palace)

Source: https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2

Volker’s repo ships **one dedicated Elmer package** (thermal). Elmer **EM** (S-parameters) reuses the same Palace layouts — only the model script ending changes (`create_elmer` + `create_elmer_run_script`), as described in his userguide.

| Folder | Type | What it is |
|--------|------|------------|
| `thermal_simplest` | Thermal | Official `more_examples/thermal_simulation_using_Elmer` (heat source + backside sink) |
| `line_viaport` | EM | Via-port microstrip line (`palace_line_viaport` → Elmer) |
| `L_2n0` | EM | 2 nH inductor (`palace_L2n0` → Elmer) |

## Prerequisites

- Python with **gds2palace ≥ 0.4.1** (`gdspy`, `gmsh`, …)
- **Elmer FEM** installed (`ElmerGrid`, `ElmerSolver`)
  - Windows: set `ELMER_HOME`
  - Linux/WSL: tools on `PATH`
- EMStudio Preferences: `ELMER_SOLVER_PATH` / Elmer Python as needed

## Run in EMStudio

1. File → Open Python Model… → e.g. `thermal_simplest/elmer_thermal_simplest_typicalvalues.py` or `line_viaport/elmer_line_viaport.py`
2. Select **Elmer** as the simulation tool
3. Simulate → Run

Thermal docs: see `thermal_simplest/Elmer_Thermal_Workflow.md` (from Volker).

These files are local downloads (`examples/` is gitignored).
