/// @file snapshot_loader.hpp
/// @brief Загрузка актуальных снапшотов по `snapshot_version` (C8): по `NOTIFY snapshot_ready`, при старте и
/// раз в минуту (уведомления, пришедшие, пока `certd` лежал или слушатель переподключался, теряются).
#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <trantor/net/EventLoopThread.h>

#include "sertkontrol_contracts.hpp"
#include "snapshot_set.hpp"

namespace sk::certd {

/// Версия, загруженная очередным `refresh`.
struct LoadedSnapshot {
  SnapshotRole role{SnapshotRole::kProd};
  std::uint64_t version{0};
};

/// Итог `refresh`: что загружено и что не открылось (у таких ролей остался прежний снапшот).
struct RefreshResult {
  std::vector<LoadedSnapshot> loaded{};
  std::vector<Error> errors{};
};

/// Открывает последние `ready`-версии каждой роли, если они новее загруженных. Файл открывается в отдельном
/// потоке: `open_snapshot` проверяет контрольную сумму всего файла, а IO-потоки Drogon блокировать нельзя
/// (АРХ §3, R3.8).
class SnapshotLoader {
 public:
  SnapshotLoader(drogon::orm::DbClientPtr db, SnapshotSet& set);
  SnapshotLoader(const SnapshotLoader&) = delete;
  SnapshotLoader& operator=(const SnapshotLoader&) = delete;
  SnapshotLoader(SnapshotLoader&&) = delete;
  SnapshotLoader& operator=(SnapshotLoader&&) = delete;
  ~SnapshotLoader();

  /// Загружает новые версии. Ошибка одной роли не мешает остальным; у роли с ошибкой остаётся старый снапшот
  /// (АРХ §4, «Отказы потока B»). `Error` — только если недоступна БД.
  drogon::Task<Result<RefreshResult>> refresh();

 private:
  drogon::orm::DbClientPtr db_;
  SnapshotSet& set_;
  std::unique_ptr<trantor::EventLoopThread> io_;
};

}  // namespace sk::certd
