/// @file fake_source.hpp
/// @brief `FakeSource` — C3 из заранее заданных записей. Заменяется демо-адаптером `data/demo` на этапе 1.
#pragma once

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "source_adapter.hpp"

namespace sk::ingest {

class FakeSource {
 public:
  FakeSource(std::vector<RawRecord> records, Date date) : records_(std::move(records)), date_(date) {}

  [[nodiscard]] static std::string_view name() noexcept { return "fake"; }
  [[nodiscard]] Date source_date() const noexcept { return date_; }

  /// Отдаёт копии записей в `sink` по порядку.
  Result<std::size_t> for_each(const RecordSink& sink) {
    for (auto record : records_) {
      sink(std::move(record));
    }
    return records_.size();
  }

 private:
  std::vector<RawRecord> records_;
  Date date_;
};

static_assert(SourceAdapter<FakeSource>);

}  // namespace sk::ingest
