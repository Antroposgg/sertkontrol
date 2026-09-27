#include "jobs.hpp"

#include <drogon/orm/Exception.h>

#include <algorithm>

#include <trantor/utils/Logger.h>

namespace sk::certd {

namespace {

Error job_db_error(const drogon::orm::DrogonDbException& e) {
  return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
}

}  // namespace

std::chrono::seconds JobQueue::backoff(int attempts) noexcept {
  constexpr std::chrono::seconds kBase{30};
  constexpr std::chrono::seconds kMax{3600};
  const int shift = std::clamp(attempts - 1, 0, 16);
  return std::min(kMax, kBase * (std::chrono::seconds::rep{1} << shift));
}

drogon::Task<Result<bool>> JobQueue::enqueue(std::string kind, std::string dedup_key, std::string payload,
                                             std::chrono::seconds delay) {
  try {
    const auto r = co_await db_->execSqlCoro(
        "INSERT INTO job (kind, dedup_key, payload, run_at) "
        "VALUES ($1, $2, $3::jsonb, now() + $4::bigint * interval '1 second') "
        "ON CONFLICT (kind, dedup_key) WHERE dedup_key IS NOT NULL DO NOTHING RETURNING id",
        kind, dedup_key, payload, static_cast<std::int64_t>(delay.count()));
    co_return !r.empty();
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return job_db_error(e);
  }
}

drogon::Task<Result<std::optional<Job>>> JobQueue::claim() {
  try {
    const auto r = co_await db_->execSqlCoro(
        "UPDATE job SET locked_until = now() + $1::bigint * interval '1 second', attempts = attempts + 1 "
        "WHERE id = (SELECT id FROM job WHERE run_at <= now() AND (locked_until IS NULL OR locked_until < "
        "now()) "
        "ORDER BY run_at, id LIMIT 1 FOR UPDATE SKIP LOCKED) "
        "RETURNING id, kind, payload::text AS payload, attempts",
        static_cast<std::int64_t>(kLease.count()));
    if (r.empty()) {
      co_return std::optional<Job>{};
    }
    co_return std::optional<Job>{Job{.id = r[0]["id"].as<std::int64_t>(),
                                     .kind = r[0]["kind"].as<std::string>(),
                                     .payload = r[0]["payload"].as<std::string>(),
                                     .attempts = r[0]["attempts"].as<int>()}};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return job_db_error(e);
  }
}

drogon::Task<Result<Ok>> JobQueue::complete(std::int64_t id) {
  try {
    co_await db_->execSqlCoro("DELETE FROM job WHERE id = $1", id);
    co_return Ok{};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return job_db_error(e);
  }
}

drogon::Task<Result<Ok>> JobQueue::fail(Job job, std::string error) {
  try {
    if (job.attempts >= kMaxAttempts) {
      LOG_ERROR << "задача " << job.kind << " #" << job.id << " исчерпала попытки: " << error;
      co_await db_->execSqlCoro(
          "UPDATE job SET locked_until = NULL, run_at = 'infinity', last_error = $2 WHERE id = $1", job.id,
          error);
    } else {
      co_await db_->execSqlCoro(
          "UPDATE job SET locked_until = NULL, run_at = now() + $2::bigint * interval '1 second', "
          "last_error = $3 WHERE id = $1",
          job.id, static_cast<std::int64_t>(backoff(job.attempts).count()), error);
    }
    co_return Ok{};
  } catch (const drogon::orm::DrogonDbException& e) {
    co_return job_db_error(e);
  }
}

drogon::Task<Result<std::size_t>> JobRunner::run_ready(std::size_t max_jobs) {
  std::size_t done = 0;
  for (std::size_t i = 0; i < max_jobs; ++i) {
    auto claimed = co_await queue_.claim();
    if (!claimed) {
      co_return claimed.error();
    }
    auto maybe_job = std::move(claimed).value();
    if (!maybe_job.has_value()) {
      break;
    }
    auto job = std::move(*maybe_job);
    const auto it = handlers_.find(job.kind);
    Result<Ok> result{Ok{}};
    if (it == handlers_.end()) {
      result = Error{ErrorCode::kInternal, "нет обработчика задачи " + job.kind};
    } else {
      result = co_await it->second(job.payload);
    }
    if (result) {
      if (auto r = co_await queue_.complete(job.id); !r) {
        co_return r.error();
      }
      ++done;
      continue;
    }
    LOG_WARN << "задача " << job.kind << " #" << job.id << " (попытка " << job.attempts
             << "): " << result.error().detail;
    if (auto r = co_await queue_.fail(std::move(job), result.error().detail); !r) {
      co_return r.error();
    }
  }
  co_return done;
}

}  // namespace sk::certd
