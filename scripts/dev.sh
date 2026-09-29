#!/usr/bin/env bash
# Запуск команды в dev-контейнере (ubuntu:24.04 + все apt-зависимости) от имени текущего пользователя.
# Пример: scripts/dev.sh scripts/ci/cpp-build-test.sh gcc-release
# SK_DEV_DOCKER_ARGS — дополнительные параметры `docker run` (разделяются пробелами), например для TSan в gate.sh.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
# Тег — хеш dev.Dockerfile, списков пакетов и install-deps.sh: правка списка пересобирает образ, а не использует старый.
tag="sertkontrol-dev:$(cat "$root/docker/dev.Dockerfile" "$root"/docker/apt-*.txt "$root/scripts/ci/install-deps.sh" | sha256sum | cut -c1-12)"
docker image inspect "$tag" >/dev/null 2>&1 ||
  docker build -f "$root/docker/dev.Dockerfile" -t "$tag" "$root"
read -r -a extra <<<"${SK_DEV_DOCKER_ARGS:-}"
exec docker run --rm "${extra[@]}" -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$root:/src" -w /src "$tag" "$@"
