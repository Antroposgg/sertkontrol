#!/usr/bin/env bash
# Конфигурация, сборка и тесты C++ по пресету. Тесты гоняются дважды (ловим флаки).
# Использование: scripts/ci/cpp-build-test.sh <gcc-release|clang-asan|coverage>
set -euo pipefail
preset="${1:?укажите пресет}"
cd "$(dirname "$0")/../.."
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"
