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
echo "GCNO files: $(find . -name '*.gcno' | wc -l | tr -d ' ')"
echo "GCDA files: $(find . -name '*.gcda' | wc -l | tr -d ' ')"
echo

TMP_OUT="$(mktemp)"
trap 'rm -f "$TMP_OUT"' EXIT

gcovr -r "$ROOT_DIR" \
  --filter ".*[\\/]src[\\/].*" \
  --exclude ".*[\\/](moc_|ui_|qrc_).*" \
  --exclude ".*[\\/]tests[\\/].*" \
  --print-summary \
  > "$TMP_OUT"

awk '
  $1 ~ /^src\// || $1 == "TOTAL" { print $1,$2,$3,$4 }
' "$TMP_OUT" | column -t

echo

grep -E '^(lines|functions|branches):' "$TMP_OUT" || true

echo
echo "=== Coverage step finished ==="
date
