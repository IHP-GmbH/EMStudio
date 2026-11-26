# Palace E2E

EMStudio script mode → **prebaked mesh** → Docker Palace → Touchstone.

Live `gds2palace`/gmsh OCC **Fragments hangs on GitHub-hosted runners** (even for a
tiny rectangle GDS). CI therefore stages `prebaked/` via
`tests/tools/palace_e2e_mesh_stub.sh`. Set `EMSTUDIO_PALACE_LIVE_MESH=1` locally
to run real meshing.

## Fixtures

- `line_viaport_smoke.gds` / `SG13G2_smoke.xml` — GUI script generation
- `prebaked/config.json` + `prebaked/smoke.msh` — solver input (from a small
  resistors example, single 1 GHz point)

## CI

- PR / `workflow_dispatch` (`palace_e2e=true`): `build.yml` job `sim-palace`
- Nightly: `.github/workflows/palace-e2e-nightly.yml`

Palace image: `ghcr.io/benvial/palace:v0.16.0`.
