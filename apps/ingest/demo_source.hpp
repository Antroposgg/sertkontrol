/// @file demo_source.hpp
/// @brief `DemoTsvSource` — C3-адаптер демо-данных `data/demo/*.tsv` (формат — data/demo/README.md).
#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

#include "source_adapter.hpp"

namespace sk::ingest {

class DemoTsvSource {
 public:
  /// Открывает файл и читает заголовок (`#source_date=YYYY-MM-DD` и строку имён колонок).
  [[nodiscard]] static Result<DemoTsvSource> open(const std::filesystem::path& file);

  [[nodiscard]] static std::string_view name() noexcept { return "demo"; }
  [[nodiscard]] Date source_date() const noexcept { return date_; }

  /// Отдаёт строки данных по одной. Строка с неверным числом колонок — ошибка набора.
  Result<std::size_t> for_each(const RecordSink& sink) const;

 private:
  DemoTsvSource(std::filesystem::path file, Date date) : file_(std::move(file)), date_(date) {}

  std::filesystem::path file_;
  Date date_;
};

static_assert(SourceAdapter<DemoTsvSource>);

}  // namespace sk::ingest
