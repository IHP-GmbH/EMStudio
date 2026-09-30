# MIM-loaded balun with dense via arrays (Palace)

Source (Volker Muehlhaus / gds2palace):
https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2/tree/main/more_examples/mesh_convergence/mesh_convergence_balun_mim

17–22 GHz balun (80 Ω SE → 100 Ω differential) with MIM capacitors. The GDS has
**~3300 TopVia1 / related via polygons** (plus metals); the model sets
`merge_polygon_size = 3.0` so via arrays are merged before meshing.

## Files

| File | Role |
|------|------|
| `trans_100diff_to_80se_ports_mesh2.py` | Palace model (2 µm mesh, order 2) |
| `trans_100diff_to_80se_ports.gds` | Layout + ports (~488 KB) |
| `SG13G2_200um.xml` | Stackup |

Top cell: `central_coils_100_tm2_7u_cm_co`.

## Run in EMStudio

1. Preferences: `PALACE_PYTHON` with `gds2palace≥0.5.2`.
2. File → Open Python Model… → `trans_100diff_to_80se_ports_mesh2.py`.
3. Simulate → Run (expect multi‑GB RAM; upstream 2 µm run ~9 GB / ~13 min on 16 cores).
