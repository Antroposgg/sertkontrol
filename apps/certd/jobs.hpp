/// @file jobs.hpp
/// @brief Очередь фоновых задач в таблице `job` (АРХ §6, ADR-0005): аренда через `FOR UPDATE SKIP LOCKED`,
/// повтор с экспоненциальной паузой, дедупликация по `(kind, dedup_key)`.
#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Взятая в работу задача.
struct Job {
  std::int64_t id{0};
  std::string kind{};
  std::string payload{};  ///< JSON-объект.
  int attempts{0};        ///< С учётом текущей попытки.
};

/// Таблица `job`. Задача, взятая в работу, недоступна другим исполнителям до конца аренды; процесс, упавший
/// посреди задачи, не теряет её — по истечении аренды задачу возьмут снова. Поэтому обработчики обязаны быть
/// идемпотентными.
class JobQueue {
 public:
  static constexpr int kMaxAttempts = 5;
  static constexpr std::chrono::seconds kLease{300};

  explicit JobQueue(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

  /// Ставит задачу на `now + delay`. Повтор с тем же `(kind, dedup_key)` игнорируется → `false`.
  drogon::Task<Result<bool>> enqueue(std::string kind, std::string dedup_key, std::string payload,
                                     std::chrono::seconds delay);
  /// Берёт самую раннюю готовую задачу с арендой `kLease`.
  drogon::Task<Result<std::optional<Job>>> claim();
  /// Задача выполнена — удаляется.
  drogon::Task<Result<Ok>> complete(std::int64_t id);
  /// Задача не выполнена: повтор через min(30 с · 2^(n−1), 1 ч); после `kMaxAttempts` попыток —
  /// `run_at = infinity` с причиной в `last_error` (для разбора вручную).
  drogon::Task<Result<Ok>> fail(Job job, std::string error);

  /// Пауза перед попыткой `attempts + 1`.
  [[nodiscard]] static std::chrono::seconds backoff(int attempts) noexcept;

 private:
  drogon::orm::DbClientPtr db_;
};

/// Обработчик задачи. Функция, а не лямбда-корутина: лямбда возвращает `Task` метода (CLAUDE.md, «Корутины»).
using JobHandler = std::function<drogon::Task<Result<Ok>>(std::string payload)>;

/// Исполнитель: берёт готовые задачи по одной и вызывает обработчик по `kind`.
class JobRunner {
 public:
  explicit JobRunner(JobQueue& queue) : queue_(queue) {}

  void on(std::string kind, JobHandler handler) { handlers_[std::move(kind)] = std::move(handler); }

  /// Выполняет до `max_jobs` готовых задач. Возвращает число выполненных успешно.
  drogon::Task<Result<std::size_t>> run_ready(std::size_t max_jobs);

 private:
  JobQueue& queue_;
  std::map<std::string, JobHandler> handlers_;
};

}  // namespace sk::certd
