#include "notify.hpp"

#include <drogon/orm/Exception.h>

#include <map>
#include <utility>

#include <json/value.h>
#include <json/writer.h>

#include "ports.hpp"
#include "sertkontrol/snapshot/lookup.hpp"

namespace sk::certd {

namespace {

std::string compact_json(const Json::Value& v) {
  Json::StreamWriterBuilder b;
  b["indentation"] = "";
  b["emitUTF8"] = true;
  return Json::writeString(b, v);
}

}  // namespace

// Не анонимное пространство имён: строки живут в кадре корутины (-Wsubobject-linkage в unity-сборке GCC).
namespace notify_detail {

struct PortfolioRow {
  std::int64_t id{0};
  std::int64_t user_id{0};
  std::int64_t max_user_id{0};
  std::string doc_key{};
  std::optional<std::string> sku{};
  snapshot::Status last_status{snapshot::Status::kUnknown};
};

}  // namespace notify_detail

drogon::Task<Result<NotifyStats>> NotifyService::notify_version(snapshot::SnapshotPtr snap,
                                                                std::size_t max_batches) {
  co_return co_await run(std::move(snap), 0, false, max_batches);
}

drogon::Task<Result<NotifyStats>> NotifyService::notify_user(snapshot::SnapshotPtr snap, std::int64_t user_id,
                                                             std::size_t max_batches) {
  co_return co_await run(std::move(snap), user_id, true, max_batches);
}

drogon::Task<Result<NotifyStats>> NotifyService::run(snapshot::SnapshotPtr snap, std::int64_t user_id,
                                                     bool demo, std::size_t max_batches) {
  if (!snap) {
    co_return Error{ErrorCode::kSnapshotUnavailable, "снапшот для уведомлений не загружен"};
  }
  const auto version = static_cast<std::int64_t>(snap->meta().version);
  NotifyStats stats;
  std::int64_t cursor = 0;
  for (std::size_t batch_no = 0; batch_no < max_batches; ++batch_no) {
    std::vector<notify_detail::PortfolioRow> rows;
    try {
      // Демо — один пользователь; боевая версия — все не-демо пользователи. Пачки по id: keyset, без OFFSET.
      const auto r = co_await db_->execSqlCoro(
          "SELECT p.id, p.user_id, u.max_user_id, p.doc_key, p.sku, p.last_status "
          "FROM portfolio_item p JOIN app_user u ON u.id = p.user_id "
          "WHERE (p.last_version IS NULL OR p.last_version < $1::bigint) AND p.id > $2::bigint "
          "AND u.is_demo = $3::boolean AND ($4::bigint = 0 OR p.user_id = $4::bigint) "
          "ORDER BY p.id LIMIT $5::bigint",
          version, cursor, demo, user_id, static_cast<std::int64_t>(batch_));
      for (const auto& row : r) {
        notify_detail::PortfolioRow p{.id = row["id"].as<std::int64_t>(),
                                      .user_id = row["user_id"].as<std::int64_t>(),
                                      .max_user_id = row["max_user_id"].as<std::int64_t>(),
                                      .doc_key = row["doc_key"].as<std::string>()};
        if (!row["sku"].isNull()) {
          p.sku = row["sku"].as<std::string>();
        }
        p.last_status = snapshot::status_from_string(row["last_status"].as<std::string>())
                            .value_or(snapshot::Status::kUnknown);
        rows.push_back(std::move(p));
      }
    } catch (const drogon::orm::DrogonDbException& e) {
      co_return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
    }
    if (rows.empty()) {
      break;
    }
    cursor = rows.back().id;

    // Сверка со снапшотом — в памяти процесса, без IO: точный поиск по отображению (АРХ §7.2).
    Json::Value items{Json::arrayValue};
    std::map<std::int64_t, ChangeNotice> notices;  // app_user.id → изменения
    for (const auto& p : rows) {
      auto status = p.last_status;
      bool changed = false;
      ChangeNotice::Item change;
      if (const auto idx = snapshot::find_index(*snap, p.doc_key)) {
        const auto rec = snap->record(*idx);
        status = rec.status;
        changed = rec.status != p.last_status;
        change = ChangeNotice::Item{.item_id = p.id,
                                    .doc_key = p.doc_key,
                                    .sku = p.sku,
                                    .before = p.last_status,
                                    .after = rec.status,
                                    .status_date = rec.status_date,
                                    .suspended_until = rec.suspended_until};
      }
      // Документа нет в новой версии — статус не трогаем: исчезновение из набора ещё не смена статуса.
      Json::Value item{Json::objectValue};
      item["id"] = static_cast<Json::Int64>(p.id);
      item["status"] = std::string{snapshot::to_string(status)};
      item["changed"] = changed;
      items.append(item);
      if (changed) {
        auto& n = notices[p.user_id];
        n.max_user_id = p.max_user_id;
        n.data_date = snap->meta().source_date;
        n.is_demo = snap->meta().is_demo;
        n.items.push_back(std::move(change));
      }
    }
    Json::Value messages{Json::arrayValue};
    for (const auto& [uid, notice] : notices) {
      for (const auto& msg : render_(notice)) {
        Json::Value m{Json::objectValue};
        m["user_id"] = static_cast<Json::Int64>(uid);
        m["payload"] = maxapi::to_outbox_json(msg);  // текст JSON; в SQL приводится к jsonb
        messages.append(m);
      }
    }
    try {
      // Одна инструкция = одна транзакция: либо вся пачка, либо ничего (см. notify.hpp).
      const auto r = co_await db_->execSqlCoro(
          "WITH x AS (SELECT * FROM jsonb_to_recordset($1::jsonb) AS x(id bigint, status text, changed "
          "boolean)), "
          "upd AS (UPDATE portfolio_item p SET last_status = x.status, last_version = $2::bigint FROM x "
          "  WHERE p.id = x.id AND (p.last_version IS NULL OR p.last_version < $2::bigint) "
          "  RETURNING p.id, p.user_id), "
          "ins AS (INSERT INTO notification (portfolio_item_id, version, kind) "
          "  SELECT upd.id, $2::bigint, $3::text FROM upd JOIN x ON x.id = upd.id WHERE x.changed "
          "  ON CONFLICT DO NOTHING RETURNING portfolio_item_id), "
          "outb AS (INSERT INTO outbox (user_id, payload, priority) "
          "  SELECT m.user_id, m.payload::jsonb, $5::smallint "
          "  FROM jsonb_to_recordset($4::jsonb) AS m(user_id bigint, payload text) "
          "  WHERE EXISTS (SELECT 1 FROM ins JOIN upd ON upd.id = ins.portfolio_item_id "
          "                WHERE upd.user_id = m.user_id) RETURNING id) "
          "SELECT (SELECT count(*) FROM upd) AS checked, (SELECT count(*) FROM ins) AS notified, "
          "(SELECT count(*) FROM outb) AS messages",
          compact_json(items), version, std::string{kKind}, compact_json(messages),
          static_cast<std::int16_t>(kPriorityNotification));
      stats.checked += r[0]["checked"].as<std::size_t>();
      stats.notified += r[0]["notified"].as<std::size_t>();
      stats.messages += r[0]["messages"].as<std::size_t>();
    } catch (const drogon::orm::DrogonDbException& e) {
      co_return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
    }
    if (rows.size() < batch_) {
      break;
    }
  }
  co_return stats;
}

}  // namespace sk::certd
