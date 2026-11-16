# Palace E2E golden (CI)

End-to-end check: EMStudio → gds2palace → Palace → Touchstone, compared to `line_viaport.s2p`.

## Prebuilt Palace (never built on every CI run)

| Item | Value |
|------|--------|
| GitHub Release tag | `palace-bin-v0.16.0` |
| Asset | `palace-linux-x64.tar.gz` |
| Layout after extract | `palace/bin/palace` (+ shared libs under `palace/`) |
| Workflow env pin | `PALACE_CI_VERSION=0.16.0` |

Publish / refresh the Release (rare) with:

```bash
# on a machine with Docker, or via Actions → "Publish Palace CI binary"
./tools/publish_palace_ci_bin.sh 0.16.0
```

Source image for packaging: `ghcr.io/benvial/palace:v0.16.0` (CPU). Bump the tag and `PALACE_CI_VERSION` together when upgrading.

## Regenerating the golden Touchstone

Requires the same Palace version as CI and `gds2palace==0.5.2`:

```bash
export PALACE_INSTALL_PATH=/path/to/palace   # contains bin/palace
export EMSTUDIO_PALACE_E2E=1
# after building emstudio_golden_tests:
./emstudio_golden_tests PalaceSimE2E -platform offscreen
# copy the produced .s2p over line_viaport.s2p (see test log for path)
```

Versions used for the checked-in golden are noted in the `!` header of `line_viaport.s2p`.
