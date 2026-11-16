#!/usr/bin/env bash
# Build palace-linux-x64.tar.gz from a prebuilt Docker image (no Palace compile).
# Usage: package_palace_ci_bin.sh [palace_version] [out_dir]
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
echo "palace in image: $BIN_IN_IMAGE"
docker cp "$CID:$BIN_IN_IMAGE" "$ROOT/bin/palace.bin"
chmod +x "$ROOT/bin/palace.bin"

# Shared libraries (skip glibc / loader).
while IFS= read -r lib; do
  [[ -z "$lib" || "$lib" == *not* ]] && continue
  case "$lib" in
    */ld-linux*.so*|*/libc.so*|*/libm.so*|*/libdl.so*|*/libpthread.so*|*/librt.so*|*/libresolv.so*) continue ;;
  esac
  base="$(basename "$lib")"
  docker cp "$CID:$lib" "$ROOT/lib/$base" 2>/dev/null || true
done < <(docker run --rm --entrypoint bash "$IMAGE" -lc "ldd '$BIN_IN_IMAGE'" \
  | awk '/=> \// {print $3} /^\t\// {print $1}')

cat > "$ROOT/bin/palace" <<'EOF'
#!/usr/bin/env bash
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$ROOT/bin/palace.bin" "$@"
EOF
chmod +x "$ROOT/bin/palace"

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
