#!/usr/bin/env bash
# CI Palace E2E: skip live gds2palace/gmsh (OCC Fragments hangs on GH runners).
# Stage a checked-in tiny mesh + config where gds2palace would, then exit 0 so
# EMStudio proceeds to the real Docker Palace launcher.
#
# EMStudio also uses PALACE_PYTHON for CSV→Touchstone (combine_extend_snp.py).
# Forward those calls to a real interpreter — do not re-stage the mesh.
set -euo pipefail

SCRIPT="${1:?model script path required}"
PY="${EMSTUDIO_TEST_PYTHON:-python3}"

case "$(basename "$SCRIPT")" in
  combine_extend_snp.py)
    exec "$PY" "$@"
    ;;
esac

DIR="$(cd "$(dirname "$SCRIPT")" && pwd)"
BASE="$(basename "$SCRIPT" .py)"
OUT="$DIR/palace_model/${BASE}_data"
HERE="$(cd "$(dirname "$0")" && pwd)"
PREBAKED="$HERE/../golden/palace_e2e/prebaked"

if [[ ! -f "$PREBAKED/config.json" || ! -f "$PREBAKED/smoke.msh" ]]; then
  echo "palace_e2e prebaked mesh missing under $PREBAKED" >&2
  exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"
cp -a "$PREBAKED/." "$OUT/"
# Match what gds2palace prints so the test / EMStudio can find the run dir.
echo "Simulation data directory:  $OUT"
exit 0
