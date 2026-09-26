/// @file pg_ports.hpp
/// @brief Порты бота на PostgreSQL и отправитель outbox.
#pragma once

#include <drogon/orm/DbClient.h>

#include <cstddef>
#include <memory>

#include "ports.hpp"
#include "sertkontrol/maxapi/bot_api.hpp"

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

/// Отправитель outbox этапа 1: по одному сообщению, в порядке приоритета, до 3 попыток с паузой 5·n с.
/// Двухуровневый token bucket и повторы с джиттером (АРХ §7.6) — этап 2.
class OutboxSender {
 public:
  static constexpr int kMaxAttempts = 3;

  OutboxSender(drogon::orm::DbClientPtr db, maxapi::BotApi& api) : db_(std::move(db)), api_(api) {}

  /// Возвращает зависшие в `sending` (упал процесс между выборкой и отметкой) обратно в очередь.
  drogon::Task<Result<std::size_t>> recover_stale();
  /// Отправляет до `max_messages` готовых сообщений. Возвращает число обработанных.
  drogon::Task<Result<std::size_t>> drain(std::size_t max_messages);

 private:
  drogon::orm::DbClientPtr db_;
  maxapi::BotApi& api_;
};

}  // namespace sk::certd
