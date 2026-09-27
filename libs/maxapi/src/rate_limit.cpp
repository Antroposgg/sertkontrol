#include "sertkontrol/maxapi/rate_limit.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sk::maxapi {

namespace {

/// Сколько вёдер чатов разрешено накопить до чистки полных.
constexpr std::size_t kEvictEvery = 1024;

TokenBucket::Clock::duration seconds_to_duration(double seconds) {
  // Округление вверх: ожидание не короче нужного, иначе после пробуждения токена ещё не будет.
  const auto ns = std::ceil(seconds * 1e9);
  return std::chrono::duration_cast<TokenBucket::Clock::duration>(
      std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(ns)});
}

}  // namespace

TokenBucket::TokenBucket(double capacity, double rate_per_second, Clock::time_point now)
    : capacity_(capacity), rate_(rate_per_second), tokens_(capacity), updated_(now) {
}

void TokenBucket::refill(Clock::time_point now) {
  if (now <= updated_) {
    return;  // часы монотонные, но вызывающий может передать старое время — токены не убавляем
  }
  const std::chrono::duration<double> elapsed = now - updated_;
  tokens_ = std::min(capacity_, tokens_ + (rate_ * elapsed.count()));
  updated_ = now;
}

TokenBucket::Clock::duration TokenBucket::wait_time(Clock::time_point now) {
  refill(now);
  if (tokens_ >= 1.0) {
    return Clock::duration::zero();
  }
  return seconds_to_duration((1.0 - tokens_) / rate_);
}

bool TokenBucket::try_take(Clock::time_point now) {
  refill(now);
  if (tokens_ < 1.0) {
    return false;
  }
  tokens_ -= 1.0;
  return true;
}

void TokenBucket::drain(Clock::time_point now) {
  refill(now);
  tokens_ = 0;
}

bool TokenBucket::full(Clock::time_point now) {
  refill(now);
  return tokens_ >= capacity_;
}

SendLimiter::SendLimiter(SendLimits limits, Clock::time_point now)
    : limits_(limits), global_(limits.global_capacity, limits.global_rate, now) {
}

Permit SendLimiter::acquire(std::int64_t chat, Clock::time_point now) {
  if (++since_eviction_ >= kEvictEvery) {
    evict_full(now);
  }
  auto [it, inserted] = chats_.try_emplace(chat, limits_.chat_capacity, limits_.chat_rate, now);
  auto& bucket = it->second;
  // Сначала чат: если ему рано, глобальный токен не тратится и другие чаты не ждут.
  if (const auto wait = bucket.wait_time(now); wait > Clock::duration::zero()) {
    return Permit{.kind = Permit::Kind::kChat, .wait = wait};
  }
  if (const auto wait = global_.wait_time(now); wait > Clock::duration::zero()) {
    return Permit{.kind = Permit::Kind::kGlobal, .wait = wait};
  }
  // Оба токена есть (проверено выше при том же `now`), списываем вместе.
  bucket.try_take(now);
  global_.try_take(now);
  return Permit{};
}

void SendLimiter::penalize(Clock::time_point now) {
  global_.drain(now);
}

void SendLimiter::evict_full(Clock::time_point now) {
  since_eviction_ = 0;
  std::erase_if(chats_, [now](auto& entry) { return entry.second.full(now); });
}

}  // namespace sk::maxapi
