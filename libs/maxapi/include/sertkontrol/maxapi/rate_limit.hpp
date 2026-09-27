/// @file rate_limit.hpp
/// @brief Двухуровневый лимитер исходящих вызовов MAX (АРХ §7.6): token bucket глобально и на чат.
///
/// Параметры по умолчанию — глобально C = 5, r = 25/с; на чат C = 1, r = 1/с. Любое окно длины τ содержит
/// не больше C + r·τ отправок (в начале окна в ведре ≤ C токенов, за окно добавится ≤ r·τ, каждая отправка
/// тратит один). При τ = 1 с это 30 запросов и 2 сообщения в чат — ровно лимиты MAX при любом способе их
/// подсчёта.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace sk::maxapi {

/// Ведро: ёмкость `capacity`, пополнение `rate` токенов в секунду, b(t) = min(C, b(t0) + r·(t − t0)).
/// Начинает полным. Не потокобезопасно.
class TokenBucket {
 public:
  using Clock = std::chrono::steady_clock;

  TokenBucket(double capacity, double rate_per_second, Clock::time_point now);

  /// Сколько ждать до появления целого токена; `0` — токен есть.
  [[nodiscard]] Clock::duration wait_time(Clock::time_point now);
  /// Списывает токен, если он есть.
  bool try_take(Clock::time_point now);
  /// Обнуляет ведро: следующий токен — через 1/r (пауза после 429).
  void drain(Clock::time_point now);
  /// Ведро полное — состояние ничем не отличается от нового ведра.
  [[nodiscard]] bool full(Clock::time_point now);

 private:
  void refill(Clock::time_point now);

  double capacity_;
  double rate_;
  double tokens_;
  Clock::time_point updated_;
};

/// Решение лимитера для одного сообщения.
struct Permit {
  enum class Kind : std::uint8_t {
    kGranted,  ///< Токены списаны из обоих вёдер — можно отправлять.
    kGlobal,  ///< Нет глобального токена: подождать `wait` и повторить.
    kChat,  ///< Нет токена чата: отложить сообщение на `wait`, другие чаты не ждут.
  };
  Kind kind{Kind::kGranted};
  TokenBucket::Clock::duration wait{};
};

/// Параметры лимитера (АРХ §7.6).
struct SendLimits {
  double global_capacity{5};
  double global_rate{25};
  double chat_capacity{1};
  double chat_rate{1};
};

/// Глобальное ведро + ведро на чат. Токены списываются из обоих сразу или ни из одного. Не потокобезопасен:
/// рассчитан на единственный отправитель outbox (АРХ §3, один экземпляр `certd`).
class SendLimiter {
 public:
  using Clock = TokenBucket::Clock;

  explicit SendLimiter(SendLimits limits = {}, Clock::time_point now = Clock::now());

  /// Разрешение на отправку в чат `chat` в момент `now`.
  [[nodiscard]] Permit acquire(std::int64_t chat, Clock::time_point now);
  /// MAX ответил 429: глобальное ведро обнуляется, отправка притормаживает.
  void penalize(Clock::time_point now);
  /// Число вёдер чатов в памяти (полные вёдра периодически удаляются).
  [[nodiscard]] std::size_t chats() const noexcept { return chats_.size(); }

 private:
  void evict_full(Clock::time_point now);

  SendLimits limits_;
  TokenBucket global_;
  std::unordered_map<std::int64_t, TokenBucket> chats_;
  std::size_t since_eviction_{0};
};

}  // namespace sk::maxapi
