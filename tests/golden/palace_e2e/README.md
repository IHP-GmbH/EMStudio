# Palace E2E (CI)

End-to-end smoke: EMStudio → gds2palace → Palace → Touchstone on
`line_simple_viaport.gds` (line + two via-port rectangles) with the thin stackup
`SG13G2_smoke.xml` (Metal1 + TopMetal2 only, **no bulk Si**).

Full `SG13G2_200um.xml` makes gmsh Fragments exceed PR time budgets.

## How CI runs Palace

`sim-palace` pulls `ghcr.io/benvial/palace:v0.16.0` via
`tests/tools/palace_docker_launcher.sh`. Pin: `PALACE_CI_VERSION=0.16.0`.

The Qt test asserts solver exit 0 and a valid 2-port `.s2p` (no golden magnitude
match). Coarse mesh: 1 GHz, large cells.

## Optional Touchstone regen

```bash
export PALACE_CI_VERSION=0.16.0
python3 tools/regen_palace_e2e_golden.py
```
