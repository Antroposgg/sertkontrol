#include "snapshot_loader.hpp"

#include <drogon/orm/Exception.h>

#include <trantor/utils/Logger.h>

namespace sk::certd {

drogon::Task<Result<bool>> SnapshotLoader::refresh() {
  std::uint64_t version = 0;
  std::string path;
  try {
    const auto r = co_await db_->execSqlCoro(
        "SELECT version, file_path FROM snapshot_version WHERE status = 'ready' ORDER BY version DESC LIMIT "
        "1");
    if (r.empty()) {
      co_return false;
    }
    version = r[0]["version"].as<std::uint64_t>();
    path = r[0]["file_path"].as<std::string>();
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
  }
  if (const auto current = holder_.get(); current && current->meta().version == version) {
    co_return false;
  }
  auto opened = snapshot::open_snapshot(path);
  if (!opened) {
    co_return opened.error();
  }
  const auto& meta = opened.value()->meta();
  if (meta.version != version) {
    co_return Error{ErrorCode::kSnapshotUnavailable,
                    "версия в файле " + std::to_string(meta.version) + " ≠ " + std::to_string(version)};
  }
  LOG_INFO << "снапшот v" << version << " загружен: " << path << ", записей " << opened.value()->size()
           << (meta.is_demo ? " (ТЕСТОВЫЕ ДАННЫЕ)" : "");
  holder_.set(std::move(opened).value());
  co_return true;
}

}  // namespace sk::certd
