# Palace E2E (nightly)

Full flow: EMStudio → gds2palace → Palace → Touchstone on
`line_simple_viaport.gds` with thin stackup `SG13G2_smoke.xml`
(Metal1 + TopMetal2, no bulk Si).

## Where it runs

Not on every PR — gmsh OCC **Fragments** is too slow on 2-vCPU runners for PR CI.

Workflow: `.github/workflows/palace-e2e-nightly.yml`

- schedule: nightly
- `workflow_dispatch`: run on demand (Actions → **Palace E2E nightly** → Run workflow)

Palace image: `ghcr.io/benvial/palace:v0.16.0` via `tests/tools/palace_docker_launcher.sh`.
Pin: `PALACE_CI_VERSION=0.16.0`.

The Qt test asserts solver exit 0 and a valid 2-port `.s2p`.

## Local

```bash
export EMSTUDIO_PALACE_E2E=1 EMSTUDIO_PALACE_DOCKER=1 EMSTUDIO_TEST_PYTHON=python3
export EMSTUDIO_PALACE_E2E_TIMEOUT_MS=1500000 QTEST_FUNCTION_TIMEOUT=1800000
# build tests, then:
./emstudio_golden_tests PalaceSimE2E -platform offscreen
```
