/// @file registry_db.hpp
/// @brief Запись версий снапшота в PostgreSQL — контракт C8 (`snapshot_version`). Синхронный libpq.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "build.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::ingest {

/// Источник переменных окружения (инъекция для тестов).
using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

/// Строка подключения libpq из `POSTGRES_HOST/PORT/DB/USER/PASSWORD` (те же переменные, что у certd).
[[nodiscard]] std::string pg_conninfo(const EnvLookup& env);

/// Соединение с БД для регистрации версий.
class RegistryDb {
 public:
  [[nodiscard]] static Result<RegistryDb> connect(const std::string& conninfo);

  /// `status = 'building'` до записи файла (строка создаётся или сбрасывается).
  [[nodiscard]] Result<Ok> mark_building(std::uint64_t version, std::string_view source, Date source_date,
                                         const std::string& file_path, bool is_demo);
  /// `status = 'ready'` со статистикой сборки.
  [[nodiscard]] Result<Ok> mark_ready(std::uint64_t version, const BuildResult& build);
  /// `status = 'failed'` с причиной в `stats.error`.
  [[nodiscard]] Result<Ok> mark_failed(std::uint64_t version, const std::string& reason);

 private:
  struct Conn;
  explicit RegistryDb(std::shared_ptr<Conn> conn) : conn_(std::move(conn)) {}
  [[nodiscard]] Result<Ok> exec(const char* sql, const std::vector<std::string>& params);

  std::shared_ptr<Conn> conn_;
};

}  // namespace sk::ingest
