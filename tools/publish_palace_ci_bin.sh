#!/usr/bin/env bash
# Package Palace from Docker and publish/replace the GitHub Release asset.
# Usage: publish_palace_ci_bin.sh [palace_version]
# Requires: docker, gh (authenticated), repo write access.
set -euo pipefail

VERSION="${1:-0.16.0}"
TAG="palace-bin-v${VERSION}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist-palace-ci"
mkdir -p "$OUT"

"$ROOT/tools/package_palace_ci_bin.sh" "$VERSION" "$OUT"
TAR="$OUT/palace-linux-x64.tar.gz"

if gh release view "$TAG" --repo IHP-GmbH/EMStudio >/dev/null 2>&1; then
  echo "Release $TAG exists — uploading/replacing asset"
  gh release upload "$TAG" "$TAR" --repo IHP-GmbH/EMStudio --clobber
else
  echo "Creating release $TAG"
  gh release create "$TAG" "$TAR" \
    --repo IHP-GmbH/EMStudio \
    --title "Palace CI binary v${VERSION}" \
    --notes "Prebuilt Linux x86_64 Palace for EMStudio \`sim-palace\` CI.

Extracts to \`palace/bin/palace\` (see \`tests/golden/palace_e2e/README.md\`).

Packaged from Docker image \`ghcr.io/benvial/palace:v${VERSION}\` — Palace is not compiled in this workflow."
fi

echo "Published $TAR as $TAG"
