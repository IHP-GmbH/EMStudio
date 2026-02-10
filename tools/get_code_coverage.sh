#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Allow override:
#   BUILD_DIR=/path/to/build ./get_code_coverage.sh
# or:
#   ./get_code_coverage.sh /path/to/build
BUILD_DIR="${BUILD_DIR_OVERRIDE:-${1:-.}}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"

cd "$BUILD_DIR"

echo "=== EMStudio code coverage ==="
echo "ROOT_DIR : $ROOT_DIR"
echo "BUILD_DIR: $BUILD_DIR"
echo

# Sanity check (helps a LOT in CI)
echo "GCNO files: $(find . -name '*.gcno' | wc -l)"
echo "GCDA files: $(find . -name '*.gcda' | wc -l)"
echo

# Always print summary so CI log is never empty
gcovr -r "$ROOT_DIR" \
  --filter ".*[\\/]src[\\/].*" \
  --exclude ".*[\\/](moc_|ui_|qrc_).*" \
  --exclude ".*[\\/]tests[\\/].*" \
  --print-summary \
| tee /dev/stderr \
| awk 'NF>=4{print $1,$2,$3,$4}' | column -t

echo
echo "=== Coverage step finished ==="
date
