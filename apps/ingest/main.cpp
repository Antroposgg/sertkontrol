/// @file main.cpp
/// @brief Точка входа `ingest`: `--demo` (пара N/N+1), `--daemon` (ежедневно в 04:00 МСК), `--once`.
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cli.hpp"
#include "demo_pair.hpp"
#include "registry_db.hpp"
#include "schedule.hpp"

namespace {

// Флаг остановки для обработчика сигнала: по стандарту из обработчика допустима только запись
// volatile std::sig_atomic_t ([support.signal]).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): состояние обработчика сигнала.
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_stop_signal(int /*signal*/) {
  g_stop = 1;
}

std::optional<std::string> process_env(std::string_view name) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): однопоточный процесс, окружение читается при старте.
  const char* v = std::getenv(std::string{name}.c_str());
  return v == nullptr ? std::nullopt : std::optional<std::string>{v};
}

int run_demo(const sk::ingest::Options& opts) {
  auto db = sk::ingest::RegistryDb::connect(sk::ingest::pg_conninfo(process_env));
  if (!db) {
    std::cerr << "ingest --demo: " << db.error().detail << '\n';
    return 1;
  }
  const auto r = sk::ingest::run_demo_pair(db.value(), opts.demo_dir, opts.snapshot_dir);
  if (!r) {
    std::cerr << "ingest --demo: " << r.error().detail << '\n';
    return 1;
  }
  const auto& p = r.value();
  std::cout << "ingest --demo: снапшот N v" << sk::ingest::kDemoBaseVersion << " — " << p.base.stats.records
            << " записей; N+1 v" << sk::ingest::kDemoNextVersion << " — " << p.next.stats.records
            << " записей; изменений N → N+1: " << p.changes.size() << " (появилось " << p.diff.added
            << ", исчезло " << p.diff.removed << ", изменилось " << p.diff.changed << "). ТЕСТОВЫЕ ДАННЫЕ\n";
  return 0;
}

int run_once() {
  // Адаптер набора ФСА появится только после подтверждения свежести и формата данных (АРХ §1, главный риск).
  std::cout << "ingest --once: источник данных ФСА не подтверждён (docs/plan.md), сборка не выполняется\n";
  return 0;
}

std::string format_utc(std::chrono::system_clock::time_point t) {
  const auto secs = std::chrono::floor<std::chrono::seconds>(t);
  const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(secs)};
  const std::chrono::hh_mm_ss hms{secs - std::chrono::floor<std::chrono::days>(secs)};
  std::string out = std::to_string(static_cast<int>(ymd.year())) + "-";
  const auto two = [](unsigned v) { return (v < 10 ? "0" : "") + std::to_string(v); };
  out += two(static_cast<unsigned>(ymd.month())) + "-" + two(static_cast<unsigned>(ymd.day())) + " " +
         two(static_cast<unsigned>(hms.hours().count())) + ":" +
         two(static_cast<unsigned>(hms.minutes().count())) + " UTC";
  return out;
}

/// Ежедневный запуск `--once` в 04:00 МСК. Сон — шагами по секунде, чтобы `docker stop` (SIGTERM)
/// завершал процесс сразу, а не по таймауту.
int run_daemon() {
  (void)std::signal(SIGTERM, on_stop_signal);
  (void)std::signal(SIGINT, on_stop_signal);
  while (g_stop == 0) {
    const auto next = sk::ingest::next_daily_run(std::chrono::system_clock::now(), sk::ingest::kDailyRunAt,
                                                 sk::ingest::kMoscowOffset);
    std::cout << "ingest --daemon: следующий запуск " << format_utc(next) << " (04:00 МСК)\n" << std::flush;
    while (g_stop == 0 && std::chrono::system_clock::now() < next) {
      std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    if (g_stop != 0) {
      break;
    }
    (void)run_once();
  }
  std::cout << "ingest --daemon: остановлен по сигналу\n";
  return 0;
}

int run(const std::vector<std::string_view>& args) {
  const auto opts = sk::ingest::parse_args(args);
  if (!opts) {
    std::cerr << "ingest: " << opts.error().detail << '\n' << sk::ingest::usage();
    return 2;
  }
  switch (opts.value().mode) {
    case sk::ingest::Mode::kHelp:
      std::cout << sk::ingest::usage();
      return 0;
    case sk::ingest::Mode::kDemo:
      return run_demo(opts.value());
    case sk::ingest::Mode::kOnce:
      return run_once();
    case sk::ingest::Mode::kDaemon:
      return run_daemon();
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    return run(args);
  } catch (const std::exception& e) {
    std::cerr << "ingest: неперехваченное исключение: " << e.what() << '\n';
  } catch (...) {
    std::cerr << "ingest: неперехваченное исключение неизвестного типа\n";
  }
  return 1;
}
