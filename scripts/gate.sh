#!/usr/bin/env bash
# Ворота-проверка (CLAUDE.md): все шаги CI локально теми же скриптами.
# C++ и gitleaks — в dev-контейнере, web — в node:24-slim, Docker/compose/миграции — на хосте.
# Требуется только Docker. Запускать из корня чистого клона.
set -euo pipefail
cd "$(dirname "$0")/.."
step() { printf '\n══════ %s ══════\n' "$*"; }
node_run() { docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/src" -w /src node:24-slim "$@"; }

step "запреты импорта";            scripts/ci/deps-check.sh
step "clang-format";               scripts/dev.sh scripts/ci/cpp-format-check.sh
step "GCC 13 Release + тесты ×2";  scripts/dev.sh scripts/ci/cpp-build-test.sh gcc-release
step "clang 18 ASan/UBSan + тесты ×2"; scripts/dev.sh scripts/ci/cpp-build-test.sh clang-asan
# TSan clang 18 при vm.mmap_rnd_bits > 28 перезапускает процесс с personality(ADDR_NO_RANDOMIZE), а seccomp-профиль
# Docker по умолчанию этот вызов запрещает. Профиль снимается только у этого одноразового контейнера; в CI TSan идёт на раннере.
step "clang 18 TSan + тесты ×2";   SK_DEV_DOCKER_ARGS="--security-opt seccomp=unconfined" \
                                   scripts/dev.sh scripts/ci/cpp-build-test.sh clang-tsan
step "fuzz smoke 60 с на цель";    scripts/dev.sh scripts/ci/fuzz-smoke.sh
step "бенчмарки против АРХ §2";    scripts/dev.sh scripts/ci/bench.sh
step "clang-tidy";                 scripts/dev.sh scripts/ci/cpp-tidy.sh
step "покрытие C++ ≥ 70%";         scripts/dev.sh scripts/ci/cpp-coverage.sh
step "миграции PG 16";             scripts/ci/db-migrations.sh
step "web";                        node_run scripts/ci/web.sh
step "линтер OpenAPI";             node_run scripts/ci/openapi-lint.sh
step "docker build --no-cache ≤ 240 с"; scripts/ci/docker-build.sh
step "compose up + /healthz";      scripts/ci/compose-smoke.sh
step "контрактные тесты C7";       scripts/ci/contract-test.sh
step "gitleaks";                   scripts/dev.sh scripts/ci/gitleaks.sh
step "ВОРОТА: все шаги зелёные"
