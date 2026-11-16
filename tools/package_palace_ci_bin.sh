#!/usr/bin/env bash
# Build palace-linux-x64.tar.gz from a prebuilt Docker image (no Palace compile).
# Usage: package_palace_ci_bin.sh [palace_version] [out_dir]
#
# Layout: palace/bin/palace (+ sibling ELFs the upstream launcher needs), palace/lib/*.
# Callers should export LD_LIBRARY_PATH="$PALACE_INSTALL_PATH/lib:$LD_LIBRARY_PATH".
set -euo pipefail

VERSION="${1:-0.16.0}"
OUT_DIR="${2:-.}"
IMAGE="${PALACE_CI_DOCKER_IMAGE:-ghcr.io/benvial/palace:v${VERSION}}"
STAGE="$(mktemp -d)"
cleanup() {
  [[ -n "${CID:-}" ]] && docker rm -f "$CID" >/dev/null 2>&1 || true
  rm -rf "$STAGE"
}
trap cleanup EXIT

echo "Pulling $IMAGE ..."
docker pull "$IMAGE"

CID="$(docker create "$IMAGE")"
ROOT="$STAGE/palace"
mkdir -p "$ROOT/bin" "$ROOT/lib"

BIN_IN_IMAGE="$(docker run --rm --entrypoint bash "$IMAGE" -lc 'command -v palace')"
BIN_DIR_IN_IMAGE="$(docker run --rm --entrypoint bash "$IMAGE" -lc 'dirname "$(command -v palace)"')"
echo "palace in image: $BIN_IN_IMAGE (dir $BIN_DIR_IN_IMAGE)"

# Whole bin dir: upstream "palace" is often a launcher that needs sibling ELFs beside it.
docker cp "$CID:$BIN_DIR_IN_IMAGE/." "$ROOT/bin/"
find "$ROOT/bin" -type f -exec chmod +x {} + 2>/dev/null || true
test -x "$ROOT/bin/palace"

# Shared libraries (skip glibc / loader). Prefer ldd on every ELF under bin/.
collect_ldd() {
  docker run --rm --entrypoint bash "$IMAGE" -lc "ldd \"$1\" 2>/dev/null" \
    | awk '/=> \// {print $3} /^\t\// {print $1}' || true
}
while IFS= read -r lib; do
  [[ -z "$lib" || "$lib" == *not* ]] && continue
  case "$lib" in
    */ld-linux*.so*|*/libc.so*|*/libm.so*|*/libdl.so*|*/libpthread.so*|*/librt.so*|*/libresolv.so*) continue ;;
  esac
  base="$(basename "$lib")"
  [[ -f "$ROOT/lib/$base" ]] && continue
  docker cp "$CID:$lib" "$ROOT/lib/$base" 2>/dev/null || true
done < <(
  collect_ldd "$BIN_IN_IMAGE"
  for f in "$ROOT/bin"/*; do
    [[ -f "$f" ]] || continue
    if head -c 4 "$f" 2>/dev/null | grep -q ELF; then
      collect_ldd "$BIN_DIR_IN_IMAGE/$(basename "$f")"
    fi
  done
)

# Helper so CI / local runs can source one line.
cat > "$ROOT/env.sh" <<'EOF'
# shellcheck shell=bash
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
EOF

cat > "$ROOT/PALACE_CI_BIN.txt" <<EOF
version=$VERSION
image=$IMAGE
packaged_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

mkdir -p "$OUT_DIR"
TAR="$(cd "$OUT_DIR" && pwd)/palace-linux-x64.tar.gz"
tar -C "$STAGE" -czf "$TAR" palace
echo "Wrote $TAR"
ls -lh "$TAR"
