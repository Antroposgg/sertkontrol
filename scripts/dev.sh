#!/usr/bin/env bash
# Запуск команды в dev-контейнере (ubuntu:24.04 + все apt-зависимости) от имени текущего пользователя.
# Пример: scripts/dev.sh scripts/ci/cpp-build-test.sh gcc-release
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
docker image inspect sertkontrol-dev >/dev/null 2>&1 ||
  docker build -f "$root/docker/dev.Dockerfile" -t sertkontrol-dev "$root"
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$root:/src" -w /src sertkontrol-dev "$@"
