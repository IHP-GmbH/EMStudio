#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

DRIVER="${SCRIPT_DIR}/klEmsDriver.py"
EMSTUDIO_BIN="${APP_DIR}/EMStudio"

if [[ ! -f "${DRIVER}" ]]; then
  echo "ERROR: Driver not found: ${DRIVER}"
  exit 1
fi

if [[ ! -x "${EMSTUDIO_BIN}" ]]; then
  echo "ERROR: EMStudio binary not found or not executable: ${EMSTUDIO_BIN}"
  exit 1
fi

# -----------------------------------------------------------------------------#
# Add EMStudio to PATH for this process only
# -----------------------------------------------------------------------------#

EMSTUDIO_DIR="$(dirname "${EMSTUDIO_BIN}")"
export PATH="${EMSTUDIO_DIR}:${PATH}"

# -----------------------------------------------------------------------------#
# Locate KLayout
# -----------------------------------------------------------------------------#

if [[ -n "${KLAYOUT_EXE:-}" ]] && [[ -x "${KLAYOUT_EXE}" ]]; then
  KLAYOUT="${KLAYOUT_EXE}"
elif command -v klayout >/dev/null 2>&1; then
  KLAYOUT="$(command -v klayout)"
else
  CANDIDATES=(
    "/usr/bin/klayout"
    "/usr/local/bin/klayout"
    "${HOME}/.local/bin/klayout"
    "${HOME}/bin/klayout"
    "${HOME}/Downloads/klayout*.AppImage"
    "${HOME}/.local/bin/klayout*.AppImage"
  )

  KLAYOUT=""
  for c in "${CANDIDATES[@]}"; do
    for p in $c; do
      if [[ -x "$p" ]]; then
        KLAYOUT="$p"
        break 2
      fi
    done
  done

  if [[ -z "${KLAYOUT}" ]]; then
    echo "ERROR: Could not find KLayout."
    echo "Install it via:"
    echo "  sudo apt install klayout"
    echo "or set:"
    echo "  export KLAYOUT_EXE=/full/path/to/klayout"
    exit 2
  fi
fi

# -----------------------------------------------------------------------------#
# Launch KLayout with EMStudio driver
# -----------------------------------------------------------------------------#

echo "Using EMStudio: ${EMSTUDIO_BIN}"
echo "Using KLayout : ${KLAYOUT}"
echo "Launching KLayout with EMStudio integration..."

exec "${KLAYOUT}" -e -rm "${DRIVER}"

