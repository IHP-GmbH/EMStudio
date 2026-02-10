#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/tests/build/Desktop_Qt_5_15_2_MinGW_64_bit-Debug"

# If you run coverage from another build dir, allow override:
# BUILD_DIR=/path/to/build ./tools/cov.sh
BUILD_DIR="${BUILD_DIR_OVERRIDE:-$BUILD_DIR}"

cd "$BUILD_DIR"

gcovr -r "$ROOT_DIR" \
  --filter ".*[\\/]src[\\/].*" \
  --exclude ".*[\\/](moc_|ui_|qrc_).*" \
  --exclude ".*[\\/]tests[\\/].*" \
| awk 'NF>=4{print $1,$2,$3,$4}' | column -t

