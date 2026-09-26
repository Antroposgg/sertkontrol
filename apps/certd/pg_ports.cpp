#include "pg_ports.hpp"

#include <drogon/orm/Exception.h>

#include <trantor/utils/Logger.h>

namespace sk::certd {

namespace {

Error db_error(const drogon::orm::DrogonDbException& e) {
  return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
}

bool retryable(ErrorCode c) {
  return c == ErrorCode::kRateLimited || c == ErrorCode::kInternal;
}

}  // namespace

drogon::Task<Result<Ok>> PgOutbox::enqueue(maxapi::OutgoingMessage msg, int priority) {
  if (auto v = maxapi::validate(msg); !v) {
    co_return v.error();
  }
  try {
    co_await db_->execSqlCoro(
        "WITH u AS (INSERT INTO app_user (max_user_id) VALUES ($1) "
        "ON CONFLICT (max_user_id) DO UPDATE SET max_user_id = EXCLUDED.max_user_id RETURNING id) "
        "INSERT INTO outbox (user_id, payload, priority) SELECT id, $2::jsonb, $3 FROM u",
        msg.max_user_id, maxapi::to_outbox_json(msg), static_cast<std::int16_t>(priority));
    co_return Ok{};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<bool>> PgInboundLog::first_seen(std::string dedup_key) {
  try {
    const auto r = co_await db_->execSqlCoro(
        "INSERT INTO inbound_update (dedup_key) VALUES ($1) ON CONFLICT DO NOTHING RETURNING dedup_key",
        dedup_key);
    co_return !r.empty();
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<void> PgInboundLog::mark_processed(std::string dedup_key, std::string error) {
  try {
    co_await db_->execSqlCoro(
        "UPDATE inbound_update SET processed_at = now(), error = nullif($2, '') WHERE dedup_key = $1",
        dedup_key, error);
  } catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << "inbound_update " << dedup_key << ": " << e.base().what();
  }
}

drogon::Task<Result<std::size_t>> OutboxSender::recover_stale() {
  try {
    const auto r = co_await db_->execSqlCoro(
        "UPDATE outbox SET status = 'pending' WHERE status = 'sending' AND not_before < now() - interval '1 "
        "minute'");
    co_return static_cast<std::size_t>(r.affectedRows());
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<std::size_t>> OutboxSender::drain(std::size_t max_messages) {
  std::size_t done = 0;
  while (done < max_messages) {
    std::int64_t id = 0;
    int attempts = 0;
    std::string payload;
    try {
      // Забираем одну строку: SKIP LOCKED не даёт двум отправителям взять одно сообщение.
      const auto r = co_await db_->execSqlCoro(
          "UPDATE outbox SET status = 'sending', attempts = attempts + 1, not_before = now() "
          "WHERE id = (SELECT id FROM outbox WHERE status = 'pending' AND not_before <= now() "
          "ORDER BY priority DESC, id LIMIT 1 FOR UPDATE SKIP LOCKED) "
          "RETURNING id, attempts, payload::text AS payload");
      if (r.empty()) {
        break;
      }
      id = r[0]["id"].as<std::int64_t>();
      attempts = r[0]["attempts"].as<int>();
      payload = r[0]["payload"].as<std::string>();
    } catch (const drogon::orm::DrogonDbException& e) {
      co_return db_error(e);
    }
    ++done;
    auto msg = maxapi::from_outbox_json(payload);
    Result<Ok> sent{Ok{}};
    if (msg) {
      sent = co_await api_.send_message(std::move(msg).value());
    } else {
      sent = msg.error();
    }
    try {
      if (sent) {
        co_await db_->execSqlCoro(
            "UPDATE outbox SET status = 'sent', sent_at = now(), last_error = NULL WHERE id = $1", id);
      } else if (retryable(sent.error().code) && attempts < kMaxAttempts) {
        co_await db_->execSqlCoro(
            "UPDATE outbox SET status = 'pending', last_error = $2, not_before = now() + $3::int * interval "
            "'5 seconds' "
            "WHERE id = $1",
            id, sent.error().detail, attempts);
      } else {
        LOG_WARN << "outbox " << id << " не отправлено: " << sent.error().detail;
        co_await db_->execSqlCoro("UPDATE outbox SET status = 'failed', last_error = $2 WHERE id = $1", id,
                                  sent.error().detail);
      }
    } catch (const drogon::orm::DrogonDbException& e) {
      co_return db_error(e);
    }
  }
  co_return done;
}

}  // namespace sk::certd
