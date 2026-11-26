#!/usr/bin/env bash
# Palace launcher for CI: run the solver inside the prebuilt Docker image.
# EMStudio script mode passes the config.json path as $1.
set -euo pipefail

CONFIG="${1:?config.json path required}"
CONFIG="$(cd "$(dirname "$CONFIG")" && pwd)/$(basename "$CONFIG")"
WORKDIR="$(dirname "$CONFIG")"
IMAGE="${PALACE_CI_DOCKER_IMAGE:-ghcr.io/benvial/palace:v${PALACE_CI_VERSION:-0.16.0}}"

# Soft wall for solver stage only (mesh is outside Docker). Nightly budget is larger.
# Host uid so post-process on the mount is not blocked by root-owned files.
PALACE_DOCKER_TIMEOUT_SEC="${PALACE_DOCKER_TIMEOUT_SEC:-900}"
RUN=(docker run --rm
  -u "$(id -u):$(id -g)"
  -v "$WORKDIR:$WORKDIR"
  -w "$WORKDIR"
  "$IMAGE"
  palace "$(basename "$CONFIG")")
if command -v timeout >/dev/null 2>&1; then
  exec timeout --signal=KILL "$PALACE_DOCKER_TIMEOUT_SEC" "${RUN[@]}"
fi
exec "${RUN[@]}"
