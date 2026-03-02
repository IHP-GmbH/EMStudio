#!/usr/bin/env bash
set -e

source /opt/venv/bin/activate

export HOME=/workspace
export TMPDIR=/tmp
export OMPI_MCA_tmpdir_base=/tmp
export OMPI_MCA_orte_tmpdir_base=/tmp
export XDG_RUNTIME_DIR=/run/user/$UID

# EMStudio defaults
CFG_DIR="$HOME/.config/EMStudio"
CFG_FILE="$CFG_DIR/EMStudioApp.conf"
mkdir -p "$CFG_DIR"

if [ ! -f "$CFG_FILE" ]; then
  cp /etc/emstudio/EMStudioApp.conf "$CFG_FILE"
fi

if [ -n "${DISPLAY:-}" ]; then
  xterm &
else
  echo "DISPLAY is not set; skip xterm"
fi

exec "$@"
