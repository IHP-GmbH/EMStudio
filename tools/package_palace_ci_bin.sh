#!/usr/bin/env bash
# Build palace-linux-x64.tar.gz from a prebuilt Docker image (no Palace compile).
# Usage: package_palace_ci_bin.sh [palace_version] [out_dir]
#
# Layout: palace/bin/palace (+ sibling ELFs), palace/lib/*.
# Callers: source palace/env.sh  (sets PATH + LD_LIBRARY_PATH).
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

docker cp "$CID:$BIN_DIR_IN_IMAGE/." "$ROOT/bin/"
find "$ROOT/bin" -type f -exec chmod +x {} + 2>/dev/null || true
test -x "$ROOT/bin/palace"

# Resolve every shared library needed by every ELF under bin/ (inside the image).
mapfile -t ELFS < <(docker run --rm --entrypoint bash "$IMAGE" -lc \
  "find '$BIN_DIR_IN_IMAGE' -maxdepth 1 -type f -executable -print")
LIBS_FILE="$STAGE/libs.txt"
: > "$LIBS_FILE"
for elf in "${ELFS[@]}"; do
  docker run --rm --entrypoint bash "$IMAGE" -lc "ldd '$elf' 2>/dev/null" \
    | awk '/=> \// {print $3} /^\t\// {print $1}' >> "$LIBS_FILE" || true
done
sort -u "$LIBS_FILE" -o "$LIBS_FILE"

while IFS= read -r lib; do
  [[ -z "$lib" || "$lib" == *not* ]] && continue
  case "$lib" in
    */ld-linux*.so*|*/libc.so*|*/libm.so*|*/libdl.so*|*/libpthread.so*|*/librt.so*|*/libresolv.so*|*/libstdc++.so*|*/libgcc_s.so*) continue ;;
  esac
  base="$(basename "$lib")"
  [[ -f "$ROOT/lib/$base" ]] && continue
  echo "lib: $lib"
  docker cp "$CID:$lib" "$ROOT/lib/$base" 2>/dev/null || true
done < "$LIBS_FILE"

# Some Spack installs keep libs only as SONAME links; copy the real file too when needed.
# Also grab common MPI / PETSc / HYPRE / MFEM / CEED trees if present beside the ELF.
EXTRA_DIRS="$(docker run --rm --entrypoint bash "$IMAGE" -lc \
  "ldd '$BIN_DIR_IN_IMAGE'/palace*.bin 2>/dev/null | awk '/=> \// {print \$3}' | xargs -r -n1 dirname | sort -u" || true)"
for d in $EXTRA_DIRS; do
  [[ -z "$d" || "$d" == /lib* || "$d" == /usr/lib* ]] && continue
  echo "extra lib dir: $d"
  # Copy *.so* from that directory (best-effort).
  docker run --rm --entrypoint bash "$IMAGE" -lc "ls -1 '$d'/*.so* 2>/dev/null" | while read -r lib; do
    [[ -z "$lib" ]] && continue
    base="$(basename "$lib")"
    [[ -f "$ROOT/lib/$base" ]] && continue
    docker cp "$CID:$lib" "$ROOT/lib/$base" 2>/dev/null || true
  done
done

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

# Sanity: ELF must see libpetsc when LD_LIBRARY_PATH is set.
if compgen -G "$ROOT/bin/palace*.bin" >/dev/null; then
  ELF="$(echo "$ROOT"/bin/palace*.bin | awk '{print $1}')"
  if [[ ! -f "$ROOT/lib/libpetsc.so.3.24" ]] && [[ ! -f "$ROOT/lib/libpetsc.so" ]]; then
    echo "WARNING: libpetsc not packaged; listing lib/:" >&2
    ls -la "$ROOT/lib" >&2 || true
  fi
fi

mkdir -p "$OUT_DIR"
TAR="$(cd "$OUT_DIR" && pwd)/palace-linux-x64.tar.gz"
tar -C "$STAGE" -czf "$TAR" palace
echo "Wrote $TAR ($(du -h "$TAR" | awk '{print $1}'))"
ls -lh "$TAR"
