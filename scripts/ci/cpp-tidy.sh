#!/usr/bin/env bash
# clang-tidy 18 по всем .cpp проекта с compile_commands.json пресета tidy.
set -euo pipefail
cd "$(dirname "$0")/../.."
cmake --preset tidy >/dev/null
mapfile -t files < <(git ls-files --cached --others --exclude-standard -- 'libs/*.cpp' 'apps/*.cpp' 'tests/*.cpp')
run-clang-tidy-18 -quiet -p build/tidy -j "$(nproc)" "${files[@]}"
