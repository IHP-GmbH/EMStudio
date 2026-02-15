#!/usr/bin/env bash
set -euo pipefail

MAJOR="${1:-1}"
TAG="v${MAJOR}.0"

# Ensure we have tags
git fetch --tags --quiet >/dev/null 2>&1 || true

if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
  # MINOR = commits since vMAJOR.0
  MINOR="$(git rev-list --count "${TAG}..HEAD")"
else
  # fallback: if tag not found, count from repo start (или ставь 0)
  MINOR="$(git rev-list --count HEAD)"
fi

echo "${MAJOR}.${MINOR}"
