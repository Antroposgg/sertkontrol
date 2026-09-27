/// @file registry_db.hpp
/// @brief Запись версий снапшота в PostgreSQL — контракт C8 (`snapshot_version`, `registry_change`,
/// `NOTIFY snapshot_ready`). Синхронный libpq.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "build.hpp"
#include "sertkontrol/snapshot/diff.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::ingest {

/// Источник переменных окружения (инъекция для тестов).
using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

/// Строка подключения libpq из `POSTGRES_HOST/PORT/DB/USER/PASSWORD` (те же переменные, что у certd).
[[nodiscard]] std::string pg_conninfo(const EnvLookup& env);

/// Канал уведомления о готовой версии (C8): `NOTIFY snapshot_ready, '<version>'`.
inline constexpr std::string_view kSnapshotReadyChannel = "snapshot_ready";

/// Что регистрируется о версии.
struct VersionInfo {
  std::uint64_t version{0};
  std::string source{};
  Date source_date{};
  std::string file_path{};
  bool is_demo{false};
  std::optional<std::int16_t> demo_stage{};  ///< 0 — демо N, 1 — демо N+1; у боевых версий пусто.
};

/// JSON-представление состояния документа для `registry_change.before/after` (C8):
/// `{"status": "...", "expiry_date": "YYYY-MM-DD"|null, "status_date": "YYYY-MM-DD"|null}`.
[[nodiscard]] std::string doc_state_json(const snapshot::DocState& state);

/// `snapshot_version.stats`: итоги сборки и, если есть, агрегаты diff (АРХ §4, поток B, шаг 2).
[[nodiscard]] std::string stats_json(const BuildResult& build, const snapshot::DiffStats* diff,
                                     std::size_t written_changes);

/// Соединение с БД для регистрации версий.
class RegistryDb {
 public:
  [[nodiscard]] static Result<RegistryDb> connect(const std::string& conninfo);

  /// `status = 'building'` до записи файла (строка создаётся или сбрасывается).
  [[nodiscard]] Result<Ok> mark_building(const VersionInfo& info);

  /// В одной транзакции: заменяет `registry_change` версии на `changes`, переводит версию в `ready` со
  /// статистикой и выполняет `NOTIFY snapshot_ready, '<version>'`. Уведомление PostgreSQL доставляет только
  /// после COMMIT, поэтому `certd` не увидит версию без её изменений (АРХ §4, поток B, шаги 2–3).
  [[nodiscard]] Result<Ok> publish(std::uint64_t version, const BuildResult& build,
                                   const std::vector<snapshot::DocChange>& changes,
                                   const snapshot::DiffStats* diff);

  /// `status = 'failed'` с причиной в `stats.error`.
  [[nodiscard]] Result<Ok> mark_failed(std::uint64_t version, const std::string& reason);

 private:
  struct Conn;
  explicit RegistryDb(std::shared_ptr<Conn> conn) : conn_(std::move(conn)) {}
  [[nodiscard]] Result<Ok> exec(const char* sql, const std::vector<std::optional<std::string>>& params);

  std::shared_ptr<Conn> conn_;
};

}  // namespace sk::ingest
