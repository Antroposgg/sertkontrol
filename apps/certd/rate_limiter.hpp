/// @file rate_limiter.hpp
/// @brief Лимит проверок на пользователя (АРХ §10 «Перегрузка проверками»): token bucket в памяти процесса.
#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace sk::certd {

/// Ведро на пользователя: ёмкость `capacity`, пополнение `capacity` токенов за `period`.
/// `certd` работает в одном экземпляре (АРХ §3), поэтому состояние в памяти достаточно.
class RateLimiter {
 public:
  using Clock = std::chrono::steady_clock;

  explicit RateLimiter(double capacity = 30, std::chrono::seconds period = std::chrono::minutes{1})
      : capacity_(capacity), rate_per_second_(capacity / static_cast<double>(period.count())) {}

  /// Списывает токен, если он есть. `now` — инъекция для тестов.
  [[nodiscard]] bool try_acquire(std::int64_t key, Clock::time_point now = Clock::now());

 private:
  struct Bucket {
    double tokens{0};
    Clock::time_point updated{};
  };
  double capacity_;
  double rate_per_second_;
  std::mutex mutex_;
  std::unordered_map<std::int64_t, Bucket> buckets_;
};

}  // namespace sk::certd
