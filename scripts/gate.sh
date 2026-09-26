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
step "clang-tidy";                 scripts/dev.sh scripts/ci/cpp-tidy.sh
step "покрытие C++ ≥ 70%";         scripts/dev.sh scripts/ci/cpp-coverage.sh
step "миграции PG 16";             scripts/ci/db-migrations.sh
step "web";                        node_run scripts/ci/web.sh
step "docker build --no-cache ≤ 240 с"; scripts/ci/docker-build.sh
step "compose up + /healthz";      scripts/ci/compose-smoke.sh
step "gitleaks";                   scripts/dev.sh scripts/ci/gitleaks.sh
step "ВОРОТА: все шаги зелёные"
