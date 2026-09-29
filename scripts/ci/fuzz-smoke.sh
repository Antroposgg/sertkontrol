#!/usr/bin/env bash
# Fuzz smoke (АРХ §10): все цели libFuzzer параллельно по SK_FUZZ_SECONDS (по умолчанию 60) секунд.
# Стартовый корпус — fuzz/corpus/<цель> (не изменяется); рабочий — build/fuzz/corpus/<цель>.
# Находка (падение, утечка, таймаут) — ненулевой код и файл crash-*/leak-*/timeout-* в build/fuzz/artifacts.
set -euo pipefail
cd "$(dirname "$0")/../.."
seconds="${SK_FUZZ_SECONDS:-60}"
cmake --preset fuzz >/dev/null
cmake --build --preset fuzz
mkdir -p build/fuzz/artifacts build/fuzz/logs
pids=()
targets=()
for bin in build/fuzz/fuzz/fuzz_*; do
  name="$(basename "$bin")"
  mkdir -p "build/fuzz/corpus/$name"
  cp -n fuzz/corpus/"$name"/* "build/fuzz/corpus/$name/" 2>/dev/null || true
  "$bin" -max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 -print_final_stats=1 \
    -artifact_prefix="build/fuzz/artifacts/$name-" "build/fuzz/corpus/$name" \
    >"build/fuzz/logs/$name.log" 2>&1 &
  pids+=("$!")
  targets+=("$name")
done
fail=0
for i in "${!pids[@]}"; do
  if wait "${pids[$i]}"; then
    runs="$(grep -o 'stat::number_of_executed_units: [0-9]*' "build/fuzz/logs/${targets[$i]}.log" | awk '{print $2}')"
    echo "fuzz: ${targets[$i]} — OK, прогонов ${runs:-?}"
  else
    echo "fuzz: ${targets[$i]} — НАХОДКА, лог build/fuzz/logs/${targets[$i]}.log" >&2
    tail -40 "build/fuzz/logs/${targets[$i]}.log" >&2
    fail=1
  fi
done
exit "$fail"
