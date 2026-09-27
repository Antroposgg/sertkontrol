/// @file ports.hpp
/// @brief Порты бота к хранилищу: очередь исходящих (outbox, C9), журнал входящих событий (дедупликация) и
/// состояние диалогов.
///
/// Реализации: PostgreSQL (`pg_ports.*`) и память (`memory_ports.hpp`, для тестов бота и webhook).
#pragma once

#include <drogon/utils/coroutine.h>

#include <chrono>
#include <cstdint>
#include <optional>
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

/// Незавершённый диалог бота (`dialog_state`, R4): что бот ждёт от пользователя следующим сообщением.
struct Dialog {
  std::string state{};  ///< Например, `awaiting_inn`.
  std::int64_t arg{0};  ///< Аргумент состояния: для `awaiting_inn` — id строки `check_log`.
};

/// Хранилище диалогов. Состояние старше `kDialogTtl` считается сброшенным (АРХ §8, R4.5).
class DialogStore {
 public:
  static constexpr std::chrono::minutes kDialogTtl{30};

  DialogStore() = default;
  DialogStore(const DialogStore&) = delete;
  DialogStore& operator=(const DialogStore&) = delete;
  DialogStore(DialogStore&&) = delete;
  DialogStore& operator=(DialogStore&&) = delete;
  virtual ~DialogStore() = default;

  /// Текущий диалог пользователя MAX или `nullopt` (нет или устарел).
  virtual drogon::Task<Result<std::optional<Dialog>>> get(std::int64_t max_user_id) = 0;
  virtual drogon::Task<Result<Ok>> set(std::int64_t max_user_id, Dialog dialog) = 0;
  virtual drogon::Task<Result<Ok>> clear(std::int64_t max_user_id) = 0;
};

}  // namespace sk::certd
