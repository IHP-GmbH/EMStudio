# Palace E2E (CI)

End-to-end smoke: EMStudio → gds2palace → Palace → Touchstone on
`line_simple_viaport.gds` (line + two via-port rectangles).

## How CI runs Palace (never compiled on every PR)

`sim-palace` pulls `ghcr.io/benvial/palace:v0.16.0` and launches it through
`tests/tools/palace_docker_launcher.sh` (EMStudio script mode). Pin:
`PALACE_CI_VERSION=0.16.0`.

Settings are intentionally very coarse (**1 GHz**, large cells) so the job stays
a short full-flow check. The Qt test asserts solver exit 0 and a valid 2-port
`.s2p` — it does not compare magnitudes to a golden file.

Optional native tarball (same image, packaged for offline/`PALACE_INSTALL_PATH`):

- Release tag: `palace-bin-v0.16.0`
- Asset: `palace-linux-x64.tar.gz` → extract to `palace/bin/palace` + `palace/lib/`
- Publish (rare): Actions → **Publish Palace CI binary**, or `./tools/publish_palace_ci_bin.sh 0.16.0`

## Optional golden Touchstone

`line_viaport.s2p` can still be regenerated for offline inspection:

```bash
export PALACE_CI_VERSION=0.16.0
python3 tools/regen_palace_e2e_golden.py
```
