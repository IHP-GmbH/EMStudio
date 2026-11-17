# Palace E2E (nightly / long PR check)

Full flow: EMStudio → gds2palace → Palace → Touchstone.

## Fixtures

- `line_viaport_smoke.gds` — short Metal1/TopMetal2 line with **real** via-port
  rectangles on layers 201/202 (the classic `line_simple_viaport.gds` uses
  zero-area lines there; gds2palace warns and OCC Fragments can hang).
- `SG13G2_smoke.xml` — Metal1 + TopMetal2 only (no bulk Si).

## Where it runs

- Nightly: `.github/workflows/palace-e2e-nightly.yml`
- Optional long PR / `workflow_dispatch` on Build EMStudio (`palace_e2e=true`)

Palace: `ghcr.io/benvial/palace:v0.16.0`. Asserts exit 0 + valid 2-port `.s2p`.
