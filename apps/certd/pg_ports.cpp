#include "pg_ports.hpp"

#include <drogon/orm/Exception.h>

#include <algorithm>
#include <random>

#include <trantor/utils/Logger.h>

namespace sk::certd {

namespace {

Error pg_error(const drogon::orm::DrogonDbException& e) {
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
    co_return pg_error(e);
  }
}

drogon::Task<Result<bool>> PgInboundLog::first_seen(std::string dedup_key) {
  try {
    const auto r = co_await db_->execSqlCoro(
        "INSERT INTO inbound_update (dedup_key) VALUES ($1) ON CONFLICT DO NOTHING RETURNING dedup_key",
        dedup_key);
    co_return !r.empty();
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return pg_error(e);
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

drogon::Task<Result<std::optional<Dialog>>> PgDialogStore::get(std::int64_t max_user_id) {
  try {
    const auto r = co_await db_->execSqlCoro(
        "SELECT d.state, coalesce((d.data->>'arg')::bigint, 0) AS arg FROM dialog_state d "
        "JOIN app_user u ON u.id = d.user_id "
        "WHERE u.max_user_id = $1 AND d.updated_at > now() - $2::bigint * interval '1 minute'",
        max_user_id, static_cast<std::int64_t>(kDialogTtl.count()));
    if (r.empty()) {
      co_return std::optional<Dialog>{};
    }
    co_return std::optional<Dialog>{
        Dialog{.state = r[0]["state"].as<std::string>(), .arg = r[0]["arg"].as<std::int64_t>()}};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return pg_error(e);
  }
}

drogon::Task<Result<Ok>> PgDialogStore::set(std::int64_t max_user_id, Dialog dialog) {
  try {
    co_await db_->execSqlCoro(
        "WITH u AS (INSERT INTO app_user (max_user_id) VALUES ($1) "
        "ON CONFLICT (max_user_id) DO UPDATE SET max_user_id = EXCLUDED.max_user_id RETURNING id) "
        "INSERT INTO dialog_state (user_id, state, data, updated_at) "
        "SELECT id, $2, jsonb_build_object('arg', $3::bigint), now() FROM u "
        "ON CONFLICT (user_id) DO UPDATE SET state = EXCLUDED.state, data = EXCLUDED.data, updated_at = "
        "now()",
        max_user_id, dialog.state, dialog.arg);
    co_return Ok{};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return pg_error(e);
  }
}

drogon::Task<Result<Ok>> PgDialogStore::clear(std::int64_t max_user_id) {
  try {
    co_await db_->execSqlCoro(
        "DELETE FROM dialog_state d USING app_user u WHERE u.id = d.user_id AND u.max_user_id = $1",
        max_user_id);
    co_return Ok{};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return pg_error(e);
  }
}

OutboxSender::OutboxSender(drogon::orm::DbClientPtr db, maxapi::BotApi& api, maxapi::SendLimiter& limiter,
                           std::function<double()> jitter)
    : db_(std::move(db)), api_(api), limiter_(limiter), jitter_(std::move(jitter)) {
}

std::function<double()> OutboxSender::default_jitter() {
  auto rng = std::make_shared<std::mt19937_64>(std::random_device{}());
  return [rng] { return std::uniform_real_distribution<double>{0.5, 1.0}(*rng); };
}

std::chrono::milliseconds OutboxSender::retry_delay(int attempts, double jitter) noexcept {
  constexpr std::chrono::milliseconds kMax{300'000};
  const int shift = std::clamp(attempts - 1, 0, 20);
  const auto base =
      std::min(kMax, std::chrono::milliseconds{1000} * (std::chrono::milliseconds::rep{1} << shift));
  const double j = std::clamp(jitter, 0.5, 1.0);
  return std::chrono::milliseconds{
      static_cast<std::chrono::milliseconds::rep>(static_cast<double>(base.count()) * j)};
}

drogon::Task<Result<std::size_t>> OutboxSender::recover_stale() {
  try {
    const auto r = co_await db_->execSqlCoro(
        "UPDATE outbox SET status = 'pending' WHERE status = 'sending' AND not_before < now() - interval '1 "
        "minute'");
    co_return static_cast<std::size_t>(r.affectedRows());
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return pg_error(e);
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
      co_return pg_error(e);
    }
    ++done;
    auto msg = maxapi::from_outbox_json(payload);
    if (!msg) {
      try {
        co_await db_->execSqlCoro("UPDATE outbox SET status = 'failed', last_error = $2 WHERE id = $1", id,
                                  msg.error().detail);
      } catch (const drogon::orm::DrogonDbException& e) {
        co_return pg_error(e);
      }
      continue;
    }
    const auto permit = limiter_.acquire(msg.value().max_user_id, Clock::now());
    if (permit.kind != maxapi::Permit::Kind::kGranted) {
      const auto wait_ms =
          std::max<std::int64_t>(1, std::chrono::ceil<std::chrono::milliseconds>(permit.wait).count());
      try {
        // Попытку не тратим. Для чата откладываются все его ожидающие сообщения разом — одинаковый
        // not_before сохраняет их порядок (приоритет, id) и не гоняет по одному через БД.
        co_await db_->execSqlCoro(
            "WITH me AS (UPDATE outbox SET status = 'pending', attempts = attempts - 1, "
            "  not_before = now() + $2::bigint * interval '1 millisecond' WHERE id = $1 RETURNING user_id) "
            "UPDATE outbox o SET not_before = greatest(o.not_before, now() + $2::bigint * interval '1 "
            "millisecond') "
            "FROM me WHERE o.user_id = me.user_id AND o.status = 'pending' AND o.id <> $1 AND $3::boolean",
            id, wait_ms, permit.kind == maxapi::Permit::Kind::kChat);
      } catch (const drogon::orm::DrogonDbException& e) {
        co_return pg_error(e);
      }
      if (permit.kind == maxapi::Permit::Kind::kGlobal) {
        break;  // глобальное ведро пусто — до следующего тика
      }
      continue;
    }
    const auto sent = co_await api_.send_message(std::move(msg).value());
    try {
      if (sent) {
        co_await db_->execSqlCoro(
            "UPDATE outbox SET status = 'sent', sent_at = now(), last_error = NULL WHERE id = $1", id);
        continue;
      }
      if (sent.error().code == ErrorCode::kRateLimited) {
        limiter_.penalize(Clock::now());
      }
      if (retryable(sent.error().code) && attempts < kMaxAttempts) {
        const auto delay = retry_delay(attempts, jitter_());
        co_await db_->execSqlCoro(
            "UPDATE outbox SET status = 'pending', last_error = $2, "
            "not_before = now() + $3::bigint * interval '1 millisecond' WHERE id = $1",
            id, sent.error().detail, static_cast<std::int64_t>(delay.count()));
      } else {
        LOG_WARN << "outbox " << id << " не отправлено: " << sent.error().detail;
        co_await db_->execSqlCoro("UPDATE outbox SET status = 'failed', last_error = $2 WHERE id = $1", id,
                                  sent.error().detail);
      }
    } catch (const drogon::orm::DrogonDbException& e) {
      co_return pg_error(e);
    }
  }
  co_return done;
}

}  // namespace sk::certd
