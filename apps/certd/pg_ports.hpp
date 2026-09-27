/// @file pg_ports.hpp
/// @brief Порты бота на PostgreSQL и отправитель outbox.
#pragma once

#include <drogon/orm/DbClient.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

#include "ports.hpp"
#include "sertkontrol/maxapi/bot_api.hpp"
#include "sertkontrol/maxapi/rate_limit.hpp"

namespace sk::certd {

/// `INSERT INTO outbox` с C9-полезной нагрузкой. Пользователь создаётся, если его ещё нет.
class PgOutbox final : public Outbox {
 public:
  explicit PgOutbox(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}
  drogon::Task<Result<Ok>> enqueue(maxapi::OutgoingMessage msg, int priority) override;

 private:
  drogon::orm::DbClientPtr db_;
};

/// `inbound_update`: первичный ключ `dedup_key` — повтор события MAX отбрасывается (АРХ §7.5).
class PgInboundLog final : public InboundLog {
 public:
  explicit PgInboundLog(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}
  drogon::Task<Result<bool>> first_seen(std::string dedup_key) override;
  drogon::Task<void> mark_processed(std::string dedup_key, std::string error) override;

 private:
  drogon::orm::DbClientPtr db_;
};

/// `dialog_state` по `app_user.id`; устаревшее состояние (старше `kDialogTtl`) не возвращается.
class PgDialogStore final : public DialogStore {
 public:
  explicit PgDialogStore(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}
  drogon::Task<Result<std::optional<Dialog>>> get(std::int64_t max_user_id) override;
  drogon::Task<Result<Ok>> set(std::int64_t max_user_id, Dialog dialog) override;
  drogon::Task<Result<Ok>> clear(std::int64_t max_user_id) override;

 private:
  drogon::orm::DbClientPtr db_;
};

/// Отправитель outbox (АРХ §4 поток B, шаг 5; §7.6): по одному сообщению в порядке приоритета, через
/// двухуровневый лимитер. Нет токена чата — сообщения этого чата откладываются, другие чаты не ждут; нет
/// глобального токена — отправка до следующего тика. 429 и 5xx — повтор через min(2^(n−1) с, 5 мин) ×
/// U(0,5; 1) (джиттер разводит повторы во времени); 429 ещё и обнуляет глобальное ведро. Прочие 4xx и
/// испорченная строка — сразу `failed`. Доставка at-least-once (АРХ §7.5).
class OutboxSender {
 public:
  static constexpr int kMaxAttempts = 8;
  using Clock = maxapi::SendLimiter::Clock;

  /// `jitter` — источник U(0,5; 1); в тестах — детерминированный.
  OutboxSender(drogon::orm::DbClientPtr db, maxapi::BotApi& api, maxapi::SendLimiter& limiter,
               std::function<double()> jitter = default_jitter());

  /// Возвращает зависшие в `sending` (упал процесс между выборкой и отметкой) обратно в очередь.
  drogon::Task<Result<std::size_t>> recover_stale();
  /// Обрабатывает до `max_messages` готовых сообщений (отправленных, отложенных или отвергнутых).
  drogon::Task<Result<std::size_t>> drain(std::size_t max_messages);

  /// Пауза перед повтором после неудачной попытки `attempts` (1-based) при множителе `jitter ∈ [0,5; 1]`.
  [[nodiscard]] static std::chrono::milliseconds retry_delay(int attempts, double jitter) noexcept;
  /// Равномерный множитель U(0,5; 1) на `std::mt19937_64`.
  [[nodiscard]] static std::function<double()> default_jitter();

 private:
  drogon::orm::DbClientPtr db_;
  maxapi::BotApi& api_;
  maxapi::SendLimiter& limiter_;
  std::function<double()> jitter_;
};

}  // namespace sk::certd
