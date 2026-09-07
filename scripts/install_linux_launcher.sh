#!/usr/bin/env bash
# Installs the user-facing EMStudio launcher into a Linux prebuilt bundle directory.
#
# The real ELF binary must already exist as <bundle_dir>/EMStudio.bin.
# This writes <bundle_dir>/EMStudio (bash wrapper) that sets Qt library/plugin
# paths and execs EMStudio.bin — so users always run ./EMStudio.
#
# Usage: install_linux_launcher.sh <bundle_dir>

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <bundle_dir>" >&2
  exit 2
fi

DIR="$1"
if [[ ! -d "${DIR}" ]]; then
  echo "ERROR: bundle directory not found: ${DIR}" >&2
  exit 1
fi

if [[ ! -e "${DIR}/EMStudio.bin" ]]; then
  echo "ERROR: expected binary at ${DIR}/EMStudio.bin" >&2
  exit 1
fi

cat > "${DIR}/EMStudio" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="${DIR}/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${DIR}/plugins/platforms"
exec "${DIR}/EMStudio.bin" "$@"
EOF

chmod +x "${DIR}/EMStudio" "${DIR}/EMStudio.bin"

# Guard against the old confusing layout.
rm -f "${DIR}/EMStudio.sh"
