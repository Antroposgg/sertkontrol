/// @file ports.hpp
/// @brief Порты бота к хранилищу: очередь исходящих (outbox, C9) и журнал входящих событий (дедупликация).
///
/// Реализации: PostgreSQL (`pg_ports.*`) и память (`memory_ports.hpp`, для тестов бота и webhook).
#pragma once

#include <drogon/utils/coroutine.h>

#include <string>

#include "sertkontrol/maxapi/message.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Приоритеты outbox: больше — раньше (АРХ §4: «Проверяю…» важнее рассылки).
inline constexpr int kPriorityProgress = 10;
inline constexpr int kPriorityReply = 5;
inline constexpr int kPriorityNotification = 0;

/// Очередь исходящих сообщений (таблица `outbox`, C9).
class Outbox {
 public:
  Outbox() = default;
  Outbox(const Outbox&) = delete;
  Outbox& operator=(const Outbox&) = delete;
  Outbox(Outbox&&) = delete;
  Outbox& operator=(Outbox&&) = delete;
  virtual ~Outbox() = default;

  /// Ставит сообщение в очередь. Невалидное по лимитам MAX — `kInvalidArgument`, в очередь не попадает.
  virtual drogon::Task<Result<Ok>> enqueue(maxapi::OutgoingMessage msg, int priority) = 0;
};

/// Журнал входящих событий webhook (`inbound_update`, АРХ §7.5).
class InboundLog {
 public:
  InboundLog() = default;
  InboundLog(const InboundLog&) = delete;
  InboundLog& operator=(const InboundLog&) = delete;
  InboundLog(InboundLog&&) = delete;
  InboundLog& operator=(InboundLog&&) = delete;
  virtual ~InboundLog() = default;

  /// `true`, если ключ встретился впервые (`INSERT … ON CONFLICT DO NOTHING`).
  virtual drogon::Task<Result<bool>> first_seen(std::string dedup_key) = 0;
  /// Отмечает обработку; пустая `error` — успех.
  virtual drogon::Task<void> mark_processed(std::string dedup_key, std::string error) = 0;
};

}  // namespace sk::certd
