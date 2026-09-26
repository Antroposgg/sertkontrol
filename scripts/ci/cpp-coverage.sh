#!/usr/bin/env bash
# Покрытие C++ по строкам: GCC --coverage + gcovr. Считаются только libs/ и apps/
# (tests/, fuzz/, bench/ исключены). Порог — SK_COVERAGE_MIN (по умолчанию 70).
set -euo pipefail
cd "$(dirname "$0")/../.."
min="${SK_COVERAGE_MIN:-70}"
find build/coverage -name '*.gcda' -delete 2>/dev/null || true
scripts/ci/cpp-build-test.sh coverage
mkdir -p build/coverage/report
gcovr --root . --object-directory build/coverage \
  --filter 'libs/' --filter 'apps/' \
  --exclude 'tests/' --exclude 'fuzz/' --exclude 'bench/' \
  --gcov-executable gcov-13 \
  --txt --txt-summary \
  --html-details build/coverage/report/index.html \
  --cobertura build/coverage/report/coverage.xml \
  --fail-under-line "$min"
