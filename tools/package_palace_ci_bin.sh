#!/usr/bin/env bash
# Build palace-linux-x64.tar.gz from a prebuilt Docker image (no Palace compile).
# Usage: package_palace_ci_bin.sh [palace_version] [out_dir]
#
# Layout: palace/bin/palace (+ sibling ELFs), palace/lib/*.so* (dereferenced).
# Callers: source palace/env.sh
set -euo pipefail

VERSION="${1:-0.16.0}"
OUT_DIR="${2:-.}"
IMAGE="${PALACE_CI_DOCKER_IMAGE:-ghcr.io/benvial/palace:v${VERSION}}"
STAGE="$(mktemp -d)"
cleanup() {
  # Files inside STAGE are often root-owned (docker write); best-effort remove.
  chmod -R u+w "$STAGE" 2>/dev/null || true
  rm -rf "$STAGE" 2>/dev/null || sudo rm -rf "$STAGE" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$OUT_DIR"
OUT_ABS="$(cd "$OUT_DIR" && pwd)"
TAR="$OUT_ABS/palace-linux-x64.tar.gz"

echo "Pulling $IMAGE ..."
docker pull "$IMAGE"

# Do the whole package inside the image so symlinks are resolved (cp -L).
# Run as the host user so the tarball is removable without root.
docker run --rm -u "$(id -u):$(id -g)" -v "$STAGE:/out" --entrypoint bash "$IMAGE" -lc '
set -euo pipefail
BIN="$(command -v palace)"
BINDIR="$(dirname "$BIN")"
echo "palace: $BIN"
mkdir -p /out/palace/bin /out/palace/lib
cp -a "$BINDIR"/. /out/palace/bin/
# Ship the image MPI launcher so LD_LIBRARY_PATH=lib/ matches mpirun (system Open MPI ABI differs).
for mpi in mpirun mpiexec orterun; do
  if command -v "$mpi" >/dev/null 2>&1; then
    cp -L "$(command -v "$mpi")" "/out/palace/bin/$mpi" 2>/dev/null || true
  fi
done
chmod -R a+rX /out/palace/bin

# Collect absolute lib paths from every ELF under bin/, then copy dereferenced.
TMPLIBS="$(mktemp)"
for f in /out/palace/bin/*; do
  [[ -f "$f" && -x "$f" ]] || continue
  if head -c 4 "$f" 2>/dev/null | grep -q ELF; then
    # ldd the file as it exists in the image path (same inode names).
    src="$BINDIR/$(basename "$f")"
    ldd "$src" 2>/dev/null | awk "/=> \\// {print \$3} /^\\t\\// {print \$1}" >> "$TMPLIBS" || true
  fi
done
sort -u "$TMPLIBS" -o "$TMPLIBS"
while IFS= read -r lib; do
  [[ -z "$lib" || "$lib" == *not* ]] && continue
  case "$lib" in
    */ld-linux*.so*|*/libc.so*|*/libm.so*|*/libdl.so*|*/libpthread.so*|*/librt.so*|*/libresolv.so*) continue ;;
  esac
  base="$(basename "$lib")"
  # Dereference so SONAME symlinks become real files in the tarball.
  if [[ -e "$lib" ]]; then
    cp -L "$lib" "/out/palace/lib/$base" 2>/dev/null || cp -a "$lib" "/out/palace/lib/$base" || true
  fi
done < "$TMPLIBS"
rm -f "$TMPLIBS"

# If a copied entry is still a symlink to a missing target, try again via readlink -f.
for link in /out/palace/lib/*; do
  [[ -L "$link" ]] || continue
  tgt="$(readlink -f "$link" 2>/dev/null || true)"
  if [[ -n "$tgt" && -f "$tgt" ]]; then
    cp -L "$tgt" "$link"
  fi
done

cat > /out/palace/env.sh <<EOF
# shellcheck shell=bash
ROOT="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
export PATH="\$ROOT/bin:\$PATH"
export LD_LIBRARY_PATH="\$ROOT/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
EOF

cat > /out/palace/PALACE_CI_BIN.txt <<EOF
version='"$VERSION"'
image='"$IMAGE"'
packaged_utc=\$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

# Quick in-container check before we tar.
# shellcheck disable=SC1091
source /out/palace/env.sh
ELF="$(echo /out/palace/bin/palace*.bin | awk "{print \$1}")"
echo "ELF=$ELF"
ldd "$ELF" | tee /out/ldd.txt
if grep -E "not found" /out/ldd.txt; then
  echo "Missing libs after packaging" >&2
  ls -la /out/palace/lib >&2
  exit 1
fi

tar -C /out -czf /out/palace-linux-x64.tar.gz palace
ls -lh /out/palace-linux-x64.tar.gz
'

cp -f "$STAGE/palace-linux-x64.tar.gz" "$TAR"
echo "Wrote $TAR"
ls -lh "$TAR"
