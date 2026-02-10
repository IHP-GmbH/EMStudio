#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${BUILD_DIR_OVERRIDE:-${1:-.}}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
cd "$BUILD_DIR"

echo "=== EMStudio code coverage ==="
echo "ROOT_DIR : $ROOT_DIR"
echo "BUILD_DIR: $BUILD_DIR"
echo

echo "GCNO files: $(find . -name '*.gcno' | wc -l | tr -d ' ')"
echo "GCDA files: $(find . -name '*.gcda' | wc -l | tr -d ' ')"
echo

TMP_OUT="$(mktemp)"
trap 'rm -f "$TMP_OUT"' EXIT

echo "[INFO] Running gcovr..."
set +e
gcovr -r "$ROOT_DIR" \
  --filter ".*[\\/]src[\\/].*" \
  --exclude ".*[\\/](moc_|ui_|qrc_).*" \
  --exclude ".*[\\/]tests[\\/].*" \
  --print-summary \
  > "$TMP_OUT"
RC=$?
set -e

echo "[INFO] gcovr exit code: $RC"
echo "[INFO] gcovr output lines: $(wc -l < "$TMP_OUT" | tr -d ' ')"
echo

# If gcovr failed, dump some output to help debugging and fail the step
if [ "$RC" -ne 0 ]; then
  echo "[ERROR] gcovr failed. First 200 lines of captured output:"
  sed -n '1,200p' "$TMP_OUT" || true
  exit "$RC"
fi

echo "[INFO] First 30 lines of captured gcovr output (sanity):"
sed -n '1,30p' "$TMP_OUT" || true
echo

echo "[INFO] Per-file table:"
awk '$1 ~ /^src\// || $1 == "TOTAL" { print $1,$2,$3,$4 }' "$TMP_OUT" | column -t || true
echo

echo "[INFO] Summary:"
grep -E '^(lines|functions|branches):' "$TMP_OUT" || true

echo
echo "=== Coverage step finished ==="
date

# tiny delay can help CI flush logs
sleep 1
