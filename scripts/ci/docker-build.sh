#!/usr/bin/env bash
# docker build --no-cache с таймером: падает, если дольше SK_DOCKER_BUILD_MAX_S (по умолчанию 240 с;
# лимит кейса — 300 с без учёта загрузки базовых образов, поэтому базовые образы тянем заранее).
set -euo pipefail
cd "$(dirname "$0")/../.."
max="${SK_DOCKER_BUILD_MAX_S:-240}"
for image in ubuntu:24.04 node:24-slim; do docker pull -q "$image" >/dev/null; done
start=$(date +%s)
docker build --no-cache ${APT_MIRROR:+--build-arg APT_MIRROR="$APT_MIRROR"} -t sertkontrol:local .
elapsed=$(( $(date +%s) - start ))
echo "docker build --no-cache: ${elapsed} с (лимит ${max} с)"
if (( elapsed > max )); then
  echo "::error::docker build занял ${elapsed} с > ${max} с" >&2
  exit 1
fi
