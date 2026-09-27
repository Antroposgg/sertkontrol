#include "maintenance.hpp"

#include <drogon/orm/Exception.h>

namespace sk::certd {

drogon::Task<Result<CleanupStats>> cleanup_retention(drogon::orm::DbClientPtr db) {
  try {
    const auto r = co_await db->execSqlCoro(
        "WITH c AS (DELETE FROM check_log WHERE created_at < now() - interval '90 days' RETURNING 1), "
        "i AS (DELETE FROM inbound_update WHERE received_at < now() - interval '7 days' RETURNING 1), "
        "o AS (DELETE FROM outbox WHERE status IN ('sent', 'failed') "
        "      AND coalesce(sent_at, created_at) < now() - interval '30 days' RETURNING 1), "
        "d AS (DELETE FROM dialog_state WHERE updated_at < now() - interval '1 day' RETURNING 1) "
        "SELECT (SELECT count(*) FROM c) AS c, (SELECT count(*) FROM i) AS i, (SELECT count(*) FROM o) AS o, "
        "(SELECT count(*) FROM d) AS d");
    co_return CleanupStats{.check_log = r[0]["c"].as<std::size_t>(),
                           .inbound_update = r[0]["i"].as<std::size_t>(),
                           .outbox = r[0]["o"].as<std::size_t>(),
                           .dialog_state = r[0]["d"].as<std::size_t>()};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
  }
}

}  // namespace sk::certd
