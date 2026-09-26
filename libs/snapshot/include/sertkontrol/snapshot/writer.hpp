/// @file writer.hpp
/// @brief Сборка файла снапшота v1 (владелец R1). Вызывает `apps/ingest`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::snapshot {

/// Запись для записи в снапшот. Номер — уже в канонической форме (`canon::canonicalize`).
struct RecordInput {
  std::string number{};
  Status status{Status::kUnknown};
  std::optional<Date> issue_date{};
  std::optional<Date> expiry_date{};
  std::optional<Date> status_date{};
  std::optional<Date> suspended_until{};
  std::string applicant_name{};
  std::string applicant_inn{};
  std::string manufacturer_name{};
  std::string product{};
  std::string tnved{};
  std::string country{};
  std::string lab_accreditation{};
  std::uint64_t registry_id{0};
};

/// Метаданные снапшота.
struct WriteOptions {
  std::uint64_t version{0};
  std::string source{};  ///< До 31 байта.
  Date source_date{};
  bool is_demo{false};
};

/// Итог сборки.
struct WriteStats {
  std::size_t records{0};
  std::size_t duplicates{0};  ///< Отброшено повторов номера (оставлена запись с поздней `status_date`).
  std::size_t strings{0};
  std::uint64_t bytes{0};
  std::uint64_t checksum{0};
};

/// Пишет снапшот атомарно: `<file>.tmp` → `fsync` → `rename` → `fsync` каталога.
/// Одинаковый вход даёт побайтно одинаковый файл.
/// Ошибки: `kInvalidArgument` (номер не канонический, пустой, длинный `source`), `kInternal` (IO).
[[nodiscard]] Result<WriteStats> write_snapshot(const std::filesystem::path& file,
                                                std::vector<RecordInput> records,
                                                const WriteOptions& options);

}  // namespace sk::snapshot
