/// @file demo_pair.hpp
/// @brief Демо-пара снапшотов N / N+1 для сценария жюри (F6, АРХ §4 «Демо-вариант потока B»).
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "build.hpp"
#include "registry_db.hpp"
#include "sertkontrol/snapshot/diff.hpp"

namespace sk::ingest {

/// Версии демо-пары фиксированы: certd различает их по `snapshot_version.demo_stage`, а не по номеру,
/// но постоянные номера делают повторную сборку при каждом старте идемпотентной (те же строки и файлы).
/// Боевые версии начнутся с 3 — с адаптером набора ФСА.
inline constexpr std::uint64_t kDemoBaseVersion = 1;
inline constexpr std::uint64_t kDemoNextVersion = 2;

/// Файлы демо-источников в `--demo-dir`.
inline constexpr std::string_view kDemoBaseFile = "base.tsv";
inline constexpr std::string_view kDemoNextFile = "next.tsv";

/// Итог сборки пары.
struct DemoPairResult {
  BuildResult base{};
  BuildResult next{};
  snapshot::DiffStats diff{};
  std::vector<snapshot::DocChange> changes{};  ///< Все изменения N → N+1 (демо маленькое — пишем все).
};

/// Собирает N (`base.tsv`, версия 1, `demo_stage = 0`) и N+1 (`next.tsv`, версия 2, `demo_stage = 1`),
/// считает diff N → N+1 и публикует обе версии: `registry_change` для N+1, `ready`, `NOTIFY snapshot_ready`.
/// Ошибка сборки помечает версию `failed`; уже опубликованная N остаётся.
[[nodiscard]] Result<DemoPairResult> run_demo_pair(RegistryDb& db, const std::filesystem::path& demo_dir,
                                                   const std::filesystem::path& snapshot_dir);

}  // namespace sk::ingest
