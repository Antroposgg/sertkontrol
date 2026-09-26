#include "rate_limiter.hpp"

#include <algorithm>

namespace sk::certd {

bool RateLimiter::try_acquire(std::int64_t key, Clock::time_point now) {
  const std::scoped_lock lock(mutex_);
  auto [it, inserted] = buckets_.try_emplace(key, Bucket{.tokens = capacity_, .updated = now});
  auto& b = it->second;
  if (!inserted) {
    // b(t) = min(C, b(t0) + r·(t − t0)) — АРХ §7.6.
    const std::chrono::duration<double> elapsed = now - b.updated;
    b.tokens = std::min(capacity_, b.tokens + (rate_per_second_ * std::max(0.0, elapsed.count())));
    b.updated = now;
  }
  if (b.tokens < 1.0) {
    return false;
  }
  b.tokens -= 1.0;
  return true;
}

}  // namespace sk::certd
