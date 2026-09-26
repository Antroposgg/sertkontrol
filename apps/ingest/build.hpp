/// @file build.hpp
/// @brief Сборка файла снапшота из адаптера источника (поток B, шаг 1 — АРХ §4).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "normalize.hpp"
#include "sertkontrol/snapshot/writer.hpp"

namespace sk::ingest {

/// Итог сборки.
struct BuildResult {
  std::filesystem::path file{};
  snapshot::WriteStats stats{};
  std::size_t rejected{0};
  std::string source{};
  Date source_date{};
};

/// Имя файла версии: `snap-<v>.bin`.
[[nodiscard]] inline std::filesystem::path snapshot_file(const std::filesystem::path& dir,
                                                         std::uint64_t version) {
  return dir / ("snap-" + std::to_string(version) + ".bin");
}

/// Нормализует записи адаптера и атомарно пишет снапшот версии `version` в `dir`.
template <SourceAdapter A>
[[nodiscard]] Result<BuildResult> build_snapshot(A& adapter, const std::filesystem::path& dir,
                                                 std::uint64_t version, bool is_demo) {
  auto normalized = normalize_all(adapter);
  if (!normalized) {
    return normalized.error();
  }
  auto& n = normalized.value();
  BuildResult out{.file = snapshot_file(dir, version),
                  .rejected = n.rejected.size(),
                  .source = std::string{adapter.name()},
                  .source_date = adapter.source_date()};
  auto stats = snapshot::write_snapshot(
      out.file, std::move(n.records),
      {.version = version, .source = out.source, .source_date = out.source_date, .is_demo = is_demo});
  if (!stats) {
    return stats.error();
  }
  out.stats = stats.value();
  return out;
}

}  // namespace sk::ingest
