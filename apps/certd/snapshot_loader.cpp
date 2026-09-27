#include "snapshot_loader.hpp"

#include <drogon/orm/Exception.h>

#include <functional>
#include <string>

#include <trantor/utils/Logger.h>

namespace sk::certd {

// Не анонимное пространство имён: тип лежит в кадре корутины, а GCC в unity-сборке запрещает поля с
// внутренней связностью (-Wsubobject-linkage).
namespace loader_detail {

struct ReadyVersion {
  SnapshotRole role{SnapshotRole::kProd};
  std::uint64_t version{0};
  std::string path{};
};

}  // namespace loader_detail

SnapshotLoader::SnapshotLoader(drogon::orm::DbClientPtr db, SnapshotSet& set)
    : db_(std::move(db)), set_(set), io_(std::make_unique<trantor::EventLoopThread>("snapshot-open")) {
  io_->run();
}

SnapshotLoader::~SnapshotLoader() {
  io_->getLoop()->quit();
  io_->wait();
}

drogon::Task<Result<RefreshResult>> SnapshotLoader::refresh() {
  std::vector<loader_detail::ReadyVersion> ready;
  try {
    // Последняя ready-версия каждой роли. Демо без demo_stage (этап 1) — снапшот N.
    const auto r = co_await db_->execSqlCoro(
        "SELECT DISTINCT ON (role) role, version, file_path FROM ("
        "  SELECT CASE WHEN NOT is_demo THEN 0 WHEN demo_stage = 1 THEN 2 ELSE 1 END AS role, version, "
        "file_path "
        "  FROM snapshot_version WHERE status = 'ready') v "
        "ORDER BY role, version DESC");
    for (const auto& row : r) {
      ready.push_back({.role = static_cast<SnapshotRole>(row["role"].as<int>()),
                       .version = row["version"].as<std::uint64_t>(),
                       .path = row["file_path"].as<std::string>()});
    }
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
  }
  RefreshResult out;
  for (auto& v : ready) {
    if (const auto current = set_.get(v.role); current && current->meta().version == v.version) {
      continue;
    }
    // std::function, а не лямбда в кадре корутины: тип лямбды без связывания ломает unity-сборку
    // (-Wsubobject-linkage), см. recognition_pool.cpp.
    std::function<Result<snapshot::SnapshotPtr>()> open = [path = v.path] {
      return snapshot::open_snapshot(path);
    };
    auto opened =
        co_await drogon::queueInLoopCoro<Result<snapshot::SnapshotPtr>>(io_->getLoop(), std::move(open));
    if (!opened) {
      LOG_WARN << "снапшот " << to_string(v.role) << " v" << v.version << ": " << opened.error().detail;
      out.errors.push_back(opened.error());
      continue;
    }
    const auto& meta = opened.value()->meta();
    if (meta.version != v.version) {
      Error err{ErrorCode::kSnapshotUnavailable,
                "версия в файле " + std::to_string(meta.version) + " ≠ " + std::to_string(v.version)};
      LOG_WARN << "снапшот " << to_string(v.role) << ": " << err.detail;
      out.errors.push_back(std::move(err));
      continue;
    }
    LOG_INFO << "снапшот " << to_string(v.role) << " v" << v.version << " загружен: " << v.path
             << ", записей " << opened.value()->size() << (meta.is_demo ? " (ТЕСТОВЫЕ ДАННЫЕ)" : "");
    set_.set(v.role, std::move(opened).value());
    out.loaded.push_back({.role = v.role, .version = v.version});
  }
  co_return out;
}

}  // namespace sk::certd
