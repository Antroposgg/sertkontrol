/// Бенчмарки против целей АРХ §2: поиск в снапшоте p99 ≤ 1 мс (точный) / ≤ 5 мс (нечёткий), diff,
/// канонизация. Снапшот синтетический: SK_BENCH_N записей (по умолчанию 1 000 000), серии распределены как в
/// реестре — по 1–2 номера на пару (серия, год), чтобы у нечёткого поиска были реальные кандидаты.
#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "sertkontrol/snapshot/diff.hpp"
#include "sertkontrol/snapshot/writer.hpp"
#include "sertkontrol/verify/fuzzy.hpp"
#include "sertkontrol_contracts.hpp"

namespace {

using sk::snapshot::Status;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr sk::Date kToday = year{2026} / month{9} / day{29};

std::size_t bench_n() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): читается один раз при подготовке.
  const char* v = std::getenv("SK_BENCH_N");
  return v == nullptr ? 1'000'000 : static_cast<std::size_t>(std::strtoull(v, nullptr, 10));
}

std::string make_number(std::mt19937& rng) {
  static constexpr std::string_view kLetters = "ABCEHKMOPTXY";
  const auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>{0, n - 1}(rng); };
  std::string n = "RUD-";
  n += kLetters[pick(kLetters.size())];
  n += kLetters[pick(kLetters.size())];
  n.append(".PA").append(std::to_string(10 + pick(90))).append(".B.");
  auto serial = std::to_string(pick(100'000));
  serial.insert(0, 5 - serial.size(), '0');
  n.append(serial).append("/").append(std::to_string(20 + pick(7)));
  return n;
}

struct Data {
  std::vector<std::string> numbers{};
  sk::snapshot::SnapshotPtr before{};
  sk::snapshot::SnapshotPtr after{};
};

/// Два снапшота: `after` отличается статусом у 1% записей (для diff).
const Data& data() {
  static const Data d = [] {
    Data out;
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): фиксированное зерно — воспроизводимые данные.
    std::mt19937 rng{20260929};
    const auto n = bench_n();
    std::vector<sk::snapshot::RecordInput> records;
    records.reserve(n);
    out.numbers.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      out.numbers.push_back(make_number(rng));
      records.push_back({.number = out.numbers.back(),
                         .status = Status::kActive,
                         .expiry_date = kToday + std::chrono::days{365},
                         .applicant_name = "ООО «БЕНЧМАРК»",
                         .product = "Продукция"});
    }
    const auto dir = std::filesystem::temp_directory_path();
    auto changed = records;
    for (std::size_t i = 0; i < changed.size(); i += 100) {
      changed[i].status = Status::kTerminated;
    }
    const auto a = dir / "sk-bench-before.bin";
    const auto b = dir / "sk-bench-after.bin";
    (void)sk::snapshot::write_snapshot(a, std::move(records),
                                       {.version = 1, .source = "bench", .source_date = kToday});
    (void)sk::snapshot::write_snapshot(b, std::move(changed),
                                       {.version = 2, .source = "bench", .source_date = kToday});
    out.before = sk::snapshot::open_snapshot(a).value();
    out.after = sk::snapshot::open_snapshot(b).value();
    std::filesystem::remove(a);  // отображения живут, пока живут SnapshotPtr
    std::filesystem::remove(b);
    return out;
  }();
  return d;
}

/// Задержки каждой итерации → счётчики p50/p99/max в микросекундах (Google Benchmark даёт только среднее).
void report(benchmark::State& state, std::vector<long long>& samples) {
  if (samples.empty()) {
    return;
  }
  std::ranges::sort(samples);
  const auto at = [&](double q) {
    const auto idx = static_cast<std::size_t>(q * static_cast<double>(samples.size() - 1));
    return static_cast<double>(samples[idx]) / 1000.0;
  };
  state.counters["p50_us"] = at(0.50);
  state.counters["p99_us"] = at(0.99);
  state.counters["max_us"] = at(1.0);
}

template <class F>
void timed(benchmark::State& state, const F& f) {
  std::vector<long long> samples;
  std::size_t i = 0;
  while (state.KeepRunning()) {
    const auto started = std::chrono::steady_clock::now();
    f(i++);
    samples.push_back(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started)
            .count());
  }
  report(state, samples);
}

void bm_canonicalize(benchmark::State& state) {
  timed(state, [](std::size_t) {
    benchmark::DoNotOptimize(sk::canon::canonicalize("ЕАЭС N RU Д-CR.РА08.В.89369/26"));
  });
}

/// Точный поиск + правила вердикта (АРХ §2: p99 ≤ 1 мс).
void bm_exact_lookup(benchmark::State& state) {
  const auto& d = data();
  timed(state, [&](std::size_t i) {
    const auto& n = d.numbers[(i * 7919) % d.numbers.size()];
    benchmark::DoNotOptimize(sk::verify::check(*d.before, {.text = n, .today = kToday}));
  });
}

/// Нечёткий поиск: искажённая цифра серии и буква O вместо нуля в теле (АРХ §2: p99 ≤ 5 мс).
void bm_fuzzy_lookup(benchmark::State& state) {
  const auto& d = data();
  timed(state, [&](std::size_t i) {
    auto n = d.numbers[(i * 7919) % d.numbers.size()];
    auto& digit = n[n.size() - 4];  // последняя цифра серии
    digit = digit == '9' ? '0' : static_cast<char>(digit + 1);
    benchmark::DoNotOptimize(sk::verify::check(*d.before, {.text = n, .today = kToday}));
  });
}

/// Номера нет в данных: полный перебор вариантов серии впустую.
void bm_not_found_lookup(benchmark::State& state) {
  const auto& d = data();
  timed(state, [&](std::size_t i) {
    const auto n = "RU D-ZZ.QQ99.Q." + std::to_string(10'000 + (i % 90'000)) + "/19";
    benchmark::DoNotOptimize(sk::verify::check(*d.before, {.text = n, .today = kToday}));
  });
}

/// Diff двух снапшотов (АРХ §7.3): один проход, O(N_old + N_new).
void bm_diff(benchmark::State& state) {
  const auto& d = data();
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(sk::snapshot::diff(*d.before, *d.after, {}));
  }
  state.counters["records"] = static_cast<double>(d.numbers.size());
}

}  // namespace

BENCHMARK(bm_canonicalize);
BENCHMARK(bm_exact_lookup);
BENCHMARK(bm_fuzzy_lookup);
BENCHMARK(bm_not_found_lookup);
BENCHMARK(bm_diff)->Unit(benchmark::kMillisecond)->Iterations(3);

BENCHMARK_MAIN();
