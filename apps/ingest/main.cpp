/// @file main.cpp
/// @brief Точка входа `ingest`. Этап 0: только разбор режимов; сами режимы — этапы 1–2 (docs/plan.md).
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

#include "cli.hpp"

namespace {

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
      std::cout << "ingest --demo: заглушка этапа 0, сборка демо-снапшотов — этап 1\n";
      return 0;
    case sk::ingest::Mode::kOnce:
      std::cout << "ingest --once: заглушка этапа 0, источник ФСА не подтверждён\n";
      return 0;
    case sk::ingest::Mode::kDaemon:
      std::cout << "ingest --daemon: заглушка этапа 0, планировщик — этап 2\n";
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
