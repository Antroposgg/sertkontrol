#!/usr/bin/env bash
# Проверка форматирования C++ (clang-format 18). С аргументом --fix — переформатировать.
set -euo pipefail
cd "$(dirname "$0")/../.."
mapfile -t files < <(git ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp')
if [[ "${1:-}" == "--fix" ]]; then
  clang-format-18 -i "${files[@]}"
else
  clang-format-18 --dry-run --Werror "${files[@]}"
fi
