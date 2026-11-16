# Palace E2E golden (CI)

End-to-end check: EMStudio → gds2palace → Palace → Touchstone, compared to `line_viaport.s2p`.

## How CI runs Palace (never compiled on every PR)

`sim-palace` pulls `ghcr.io/benvial/palace:v0.16.0` and launches it through
`tests/tools/palace_docker_launcher.sh` (EMStudio script mode). Pin:
`PALACE_CI_VERSION=0.16.0`.

The case is a short line with two via-port rectangles, **one frequency (5 GHz)**
and a coarse mesh — full flow smoke, typically a few minutes (CI allows up to ~7).

Optional native tarball (same image, packaged for offline/`PALACE_INSTALL_PATH`):

- Release tag: `palace-bin-v0.16.0`
- Asset: `palace-linux-x64.tar.gz` → extract to `palace/bin/palace` + `palace/lib/`
- Publish (rare): Actions → **Publish Palace CI binary**, or `./tools/publish_palace_ci_bin.sh 0.16.0`

## Regenerating the golden Touchstone

Needs Docker, `gds2palace==0.5.2`, and `scikit-rf`:

```bash
export PALACE_CI_VERSION=0.16.0
python3 tools/regen_palace_e2e_golden.py
# writes tests/golden/palace_e2e/line_viaport.s2p
```

The `!` header in `line_viaport.s2p` records the image / gds2palace versions.
