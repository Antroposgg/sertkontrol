#!/usr/bin/env bash
# Бенчмарки против целей АРХ §2 (Google Benchmark): точный поиск p99 ≤ 1 мс, нечёткий p99 ≤ 5 мс.
# SK_BENCH_N — размер синтетического снапшота (по умолчанию 1 000 000). Отчёт — build/bench/report.json.
set -euo pipefail
cd "$(dirname "$0")/../.."
cmake --preset bench >/dev/null
cmake --build --preset bench
report=build/bench/report.json
build/bench/bench/search_bench --benchmark_min_time=1s --benchmark_out="$report" --benchmark_out_format=json
python3 - "$report" <<'PY'
import json, sys
# Цели АРХ §2 (p99, микросекунды).
targets = {"bm_exact_lookup": 1000.0, "bm_fuzzy_lookup": 5000.0, "bm_not_found_lookup": 5000.0}
results = {b["name"].split("/")[0]: b for b in json.load(open(sys.argv[1]))["benchmarks"]}
fail = False
for name, limit in targets.items():
    p99 = results[name]["p99_us"]
    ok = p99 <= limit
    fail |= not ok
    print(f"bench: {name}: p50 {results[name]['p50_us']:.1f} мкс, p99 {p99:.1f} мкс (цель ≤ {limit:.0f}) — {'OK' if ok else 'ПРЕВЫШЕНО'}")
diff = results["bm_diff"]
print(f"bench: bm_diff: {diff['real_time']:.0f} мс на {int(diff['records'])} записей")
sys.exit(1 if fail else 0)
PY
