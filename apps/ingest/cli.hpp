/// @file cli.hpp
/// @brief Разбор аргументов `ingest` (режимы — АРХ §3).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::ingest {

/// Режим запуска.
enum class Mode : std::uint8_t {
  kOnce,    ///< `--once` — однократная сборка из источника.
  kDaemon,  ///< `--daemon` — ежедневно в 04:00 МСК.
  kDemo,    ///< `--demo` — собрать демо-пару N/N+1 из `data/demo`.
  kHelp,    ///< `--help`.
};

/// Разобранные параметры.
struct Options {
  Mode mode{Mode::kHelp};
  std::filesystem::path snapshot_dir{"/data/snapshots"};    ///< `--snapshot-dir <путь>`.
  std::filesystem::path demo_dir{"/opt/sertkontrol/demo"};  ///< `--demo-dir <путь>`.
};

/// Разбирает аргументы (без имени программы). Ровно один режим обязателен.
[[nodiscard]] Result<Options> parse_args(std::span<const std::string_view> args);

/// Текст справки.
[[nodiscard]] std::string_view usage() noexcept;

}  // namespace sk::ingest
