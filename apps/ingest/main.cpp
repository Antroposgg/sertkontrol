/// @file main.cpp
/// @brief Точка входа `ingest`. `--demo` — этап 1; `--once` и `--daemon` — после подтверждения источника /
/// этап 2.
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "build.hpp"
#include "cli.hpp"
#include "demo_source.hpp"
#include "registry_db.hpp"

namespace {

/// Версия демо-снапшота N. N+1 (версия 2) — этап 2.
constexpr std::uint64_t kDemoBaseVersion = 1;

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
  const auto file = sk::ingest::snapshot_file(opts.snapshot_dir, kDemoBaseVersion);
  auto source = sk::ingest::DemoTsvSource::open(opts.demo_dir / "base.tsv");
  if (!source) {
    std::cerr << "ingest --demo: " << source.error().detail << '\n';
    return 1;
  }
  if (auto r = db.value().mark_building(kDemoBaseVersion, sk::ingest::DemoTsvSource::name(),
                                        source.value().source_date(), file.string(), true);
      !r) {
    std::cerr << "ingest --demo: " << r.error().detail << '\n';
    return 1;
  }
  auto built = sk::ingest::build_snapshot(source.value(), opts.snapshot_dir, kDemoBaseVersion, true);
  if (!built) {
    std::cerr << "ingest --demo: сборка не удалась: " << built.error().detail << '\n';
    (void)db.value().mark_failed(kDemoBaseVersion, built.error().detail);
    return 1;
  }
  if (auto r = db.value().mark_ready(kDemoBaseVersion, built.value()); !r) {
    std::cerr << "ingest --demo: " << r.error().detail << '\n';
    return 1;
  }
  const auto& b = built.value();
  std::cout << "ingest --demo: снапшот v" << kDemoBaseVersion << " готов: " << b.file.string() << ", записей "
            << b.stats.records << ", отвергнуто " << b.rejected << ", дублей " << b.stats.duplicates << ", "
            << b.stats.bytes << " байт (ТЕСТОВЫЕ ДАННЫЕ)\n";
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
      std::cout
          << "ingest --once: источник данных ФСА не подтверждён (docs/plan.md), сборка не выполняется\n";
      return 0;
    case sk::ingest::Mode::kDaemon:
      std::cout << "ingest --daemon: планировщик — этап 2\n";
      return 0;
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
