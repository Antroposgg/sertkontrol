/// @file snapshot_loader.hpp
/// @brief Загрузка актуального снапшота по `snapshot_version` (C8). Этап 1 — опрос; этап 2 — `LISTEN
/// snapshot_ready`.
#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <cstdint>

#include "sertkontrol/snapshot/holder.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Открывает последнюю версию со статусом `ready`, если она новее загруженной.
class SnapshotLoader {
 public:
  SnapshotLoader(drogon::orm::DbClientPtr db, snapshot::SnapshotHolder& holder)
      : db_(std::move(db)), holder_(holder) {}

  /// `true` — загружена новая версия, `false` — без изменений. При ошибке старый снапшот остаётся (АРХ §4).
  drogon::Task<Result<bool>> refresh();

 private:
  drogon::orm::DbClientPtr db_;
  snapshot::SnapshotHolder& holder_;
};

}  // namespace sk::certd
