#!/usr/bin/env bash

set -u

REPORT_NAME="coverage.html"
TEST_LOG="test_results.txt"
BIN_NAME="emstudio_golden_tests"
FOUND_EXE=""

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-tests"
TEST_OBJECT_DIR=""
ROOT_FWD="$ROOT_DIR"

find_exe() {
    local search_dir="$1"

    if [[ -d "$search_dir" ]]; then
        FOUND_EXE="$(find "$search_dir" -type f -name "$BIN_NAME" | head -n 1)"
    fi
}

find_exe "$BUILD_DIR"

if [[ -z "$FOUND_EXE" ]]; then
    echo "Error: $BIN_NAME not found."
    exit 1
fi

TEST_OBJECT_DIR="$(dirname "$FOUND_EXE")"

echo "Cleaning old coverage data..."
find "$ROOT_DIR" -type f \( -name "*.gcda" -o -name "*.gcov" \) -delete 2>/dev/null

echo "Running: \"$FOUND_EXE\""

rm -f "$ROOT_DIR/$TEST_LOG"

pushd "$ROOT_DIR" >/dev/null || exit 1
"$FOUND_EXE"
TEST_EXIT=$?
popd >/dev/null || exit 1

if [[ -f "$ROOT_DIR/$TEST_LOG" ]]; then
    echo "Merged test log created: \"$ROOT_DIR/$TEST_LOG\""
else
    echo "Warning: $TEST_LOG was not created."
fi

echo "Test exit code: $TEST_EXIT"
echo "Continuing with coverage generation..."

pushd "$ROOT_DIR" >/dev/null || exit 1

echo "Using object directory: \"$TEST_OBJECT_DIR\""
echo "Using source root: \"$ROOT_FWD\""

python3 -m gcovr -j 1 \
  -r "$ROOT_FWD" \
  --object-directory "$TEST_OBJECT_DIR" \
  --gcov-ignore-errors=all \
  --filter "$ROOT_FWD/src/.*" \
  --exclude "$ROOT_FWD/tests/.*" \
  --exclude "$ROOT_FWD/build-tests/.*" \
  --html-details -o "$REPORT_NAME" \
  --print-summary

GCOVR_EXIT=$?

if [[ -f "$REPORT_NAME" ]]; then
    xdg-open "$REPORT_NAME" >/dev/null 2>&1 &
    find "$ROOT_DIR" -type f -name "*.gcov" -delete 2>/dev/null
else
    echo "Error: $REPORT_NAME not generated."
fi

popd >/dev/null || exit 1

if [[ $GCOVR_EXIT -ne 0 ]]; then
    exit "$GCOVR_EXIT"
fi

exit "$TEST_EXIT"

