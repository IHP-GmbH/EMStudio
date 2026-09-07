#!/usr/bin/env bash
# Verifies scripts/install_linux_launcher.sh: EMStudio launcher → EMStudio.bin.
#
# Run from repo root or any cwd; resolves paths relative to this script.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
INSTALL="${ROOT_DIR}/scripts/install_linux_launcher.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[[ -x "${INSTALL}" || -f "${INSTALL}" ]] || fail "missing ${INSTALL}"
chmod +x "${INSTALL}"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

mkdir -p "${TMP}/lib" "${TMP}/plugins/platforms"

# Stub binary: record argv and exit 0 (stand-in for the real ELF).
cat > "${TMP}/EMStudio.bin" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$@" > "$(dirname "$0")/stub_argv.txt"
printf 'LD_LIBRARY_PATH=%s\n' "${LD_LIBRARY_PATH:-}" > "$(dirname "$0")/stub_env.txt"
printf 'QT_PLUGIN_PATH=%s\n' "${QT_PLUGIN_PATH:-}" >> "$(dirname "$0")/stub_env.txt"
exit 0
EOF
chmod +x "${TMP}/EMStudio.bin"

"${INSTALL}" "${TMP}"

[[ -x "${TMP}/EMStudio" ]] || fail "EMStudio launcher not executable"
[[ -x "${TMP}/EMStudio.bin" ]] || fail "EMStudio.bin not executable"
[[ ! -e "${TMP}/EMStudio.sh" ]] || fail "EMStudio.sh must not exist"

head -n 1 "${TMP}/EMStudio" | grep -q '^#!' || fail "launcher missing shebang"
grep -q 'EMStudio\.bin' "${TMP}/EMStudio" || fail "launcher must exec EMStudio.bin"
grep -q 'LD_LIBRARY_PATH' "${TMP}/EMStudio" || fail "launcher must set LD_LIBRARY_PATH"
grep -q 'QT_PLUGIN_PATH' "${TMP}/EMStudio" || fail "launcher must set QT_PLUGIN_PATH"

# Running the launcher must invoke the stub with args and Qt env set.
"${TMP}/EMStudio" --smoke arg2
[[ -f "${TMP}/stub_argv.txt" ]] || fail "stub binary was not executed"
grep -qx -- '--smoke' "${TMP}/stub_argv.txt" || fail "arg --smoke not forwarded"
grep -qx -- 'arg2' "${TMP}/stub_argv.txt" || fail "arg arg2 not forwarded"

grep -q "${TMP}/lib" "${TMP}/stub_env.txt" || fail "LD_LIBRARY_PATH missing bundle lib"
grep -q "${TMP}/plugins" "${TMP}/stub_env.txt" || fail "QT_PLUGIN_PATH missing bundle plugins"

# install script must reject a dir without EMStudio.bin
BAD="$(mktemp -d)"
if "${INSTALL}" "${BAD}" 2>/dev/null; then
  rm -rf "${BAD}"
  fail "install should fail without EMStudio.bin"
fi
rm -rf "${BAD}"

echo "PASS: Linux launcher EMStudio → EMStudio.bin"
