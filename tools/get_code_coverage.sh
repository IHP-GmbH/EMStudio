#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${BUILD_DIR_OVERRIDE:-${1:-.}}"

BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"

cd "$BUILD_DIR"

gcovr -r "$ROOT_DIR" \
  --filter ".*[\\/]src[\\/].*" \
  --exclude ".*[\\/](moc_|ui_|qrc_).*" \
  --exclude ".*[\\/]tests[\\/].*" \
| awk 'NF>=4{print $1,$2,$3,$4}' | column -t
