/// Лимитер исходящих (АРХ §7.6): формула ведра и оценка «в любом окне τ не больше C + r·τ отправок».
#include "sertkontrol/maxapi/rate_limit.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <random>
#include <vector>

namespace sk::maxapi {
namespace {

using Clock = TokenBucket::Clock;
using std::chrono::milliseconds;

constexpr Clock::time_point kStart{};

TEST(TokenBucket, StartsFullAndRefillsAtRate) {
  TokenBucket b{2, 1, kStart};
  EXPECT_TRUE(b.full(kStart));
  EXPECT_TRUE(b.try_take(kStart));
  EXPECT_TRUE(b.try_take(kStart));
  EXPECT_FALSE(b.try_take(kStart));
  EXPECT_EQ(b.wait_time(kStart), std::chrono::seconds{1});
  EXPECT_FALSE(b.try_take(kStart + milliseconds{999}));
  EXPECT_TRUE(b.try_take(kStart + milliseconds{1000}));
  // Не больше ёмкости, сколько ни жди.
  EXPECT_TRUE(b.full(kStart + std::chrono::hours{1}));
  EXPECT_TRUE(b.try_take(kStart + std::chrono::hours{1}));
  EXPECT_TRUE(b.try_take(kStart + std::chrono::hours{1}));
  EXPECT_FALSE(b.try_take(kStart + std::chrono::hours{1}));
}

TEST(TokenBucket, OldTimeDoesNotRemoveTokensAndDrainEmpties) {
  TokenBucket b{1, 10, kStart + std::chrono::seconds{5}};
  EXPECT_EQ(b.wait_time(kStart), Clock::duration::zero());  // время из прошлого — без изменений
  b.drain(kStart + std::chrono::seconds{5});
  EXPECT_EQ(b.wait_time(kStart + std::chrono::seconds{5}), milliseconds{100});
}

TEST(SendLimiter, ChatWaitDoesNotSpendGlobalToken) {
  SendLimiter lim{{.global_capacity = 1, .global_rate = 1, .chat_capacity = 1, .chat_rate = 1}, kStart};
  EXPECT_EQ(lim.acquire(1, kStart).kind, Permit::Kind::kGranted);
  const auto p = lim.acquire(1, kStart);
  EXPECT_EQ(p.kind, Permit::Kind::kChat);
  EXPECT_EQ(p.wait, std::chrono::seconds{1});
  // Глобальный токен кончился на первой отправке — другой чат ждёт глобальное ведро.
  EXPECT_EQ(lim.acquire(2, kStart).kind, Permit::Kind::kGlobal);
  EXPECT_EQ(lim.acquire(2, kStart + std::chrono::seconds{1}).kind, Permit::Kind::kGranted);
}

TEST(SendLimiter, PenalizePausesEveryone) {
  SendLimiter lim{{}, kStart};
  lim.penalize(kStart);
  const auto p = lim.acquire(7, kStart);
  EXPECT_EQ(p.kind, Permit::Kind::kGlobal);
  EXPECT_EQ(p.wait, milliseconds{40});  // 1 / 25 с
}

TEST(SendLimiter, FullChatBucketsAreEvicted) {
  SendLimiter lim{{}, kStart};
  for (std::int64_t chat = 0; chat < 2000; ++chat) {
    (void)lim.acquire(chat, kStart + std::chrono::seconds{chat});
  }
  // Каждые 1024 вызова полные (давно не писавшие) чаты удаляются — память не растёт с числом чатов.
  EXPECT_LT(lim.chats(), 1100U);
}

/// Property: при любом потоке запросов в любом окне [t, t + τ] не больше C + r·τ отправок — глобально и в
/// каждый чат. Отправитель ведёт себя как настоящий: ждёт глобальный токен, откладывает сообщение чата.
TEST(SendLimiter, WindowBoundHoldsForRandomTraffic) {
  const SendLimits limits{};  // C = 5, r = 25; на чат C = 1, r = 1
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): фиксированное зерно — воспроизводимый property-тест.
  std::mt19937 rng{7};
  std::uniform_int_distribution<int> chat{1, 6};
  std::exponential_distribution<double> gap{40.0};  // ~40 попыток в секунду — больше лимита
  SendLimiter lim{limits, kStart};
  std::vector<std::pair<Clock::time_point, std::int64_t>> sent;
  auto now = kStart;
  std::deque<std::int64_t> queue;
  for (int i = 0; i < 8000; ++i) {
    now += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{gap(rng)});
    queue.push_back(chat(rng));
    // Обрабатываем очередь, пока лимитер разрешает; отложенные чаты — в конец.
    // Не больше 12 попыток за шаг: очередь растёт (поток выше лимита), полный проход сделал бы тест O(n²).
    for (std::size_t tries = std::min<std::size_t>(queue.size(), 12); tries > 0 && !queue.empty(); --tries) {
      const auto c = queue.front();
      queue.pop_front();
      const auto p = lim.acquire(c, now);
      if (p.kind == Permit::Kind::kGranted) {
        sent.emplace_back(now, c);
      } else {
        queue.push_back(c);
        if (p.kind == Permit::Kind::kGlobal) {
          break;
        }
      }
    }
  }
  ASSERT_GT(sent.size(), 1000U);
  const auto check_windows = [](const std::vector<Clock::time_point>& times, double capacity, double rate) {
    for (const double tau : {0.0, 0.1, 0.5, 1.0, 3.0}) {
      const auto window = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{tau});
      const auto bound = static_cast<std::size_t>(std::floor(capacity + (rate * tau) + 1e-9));
      std::size_t lo = 0;
      for (std::size_t hi = 0; hi < times.size(); ++hi) {
        while (times[hi] - times[lo] > window) {
          ++lo;
        }
        ASSERT_LE(hi - lo + 1, bound) << "τ = " << tau;
      }
    }
  };
  std::vector<Clock::time_point> all;
  std::map<std::int64_t, std::vector<Clock::time_point>> per_chat;
  for (const auto& [t, c] : sent) {
    all.push_back(t);
    per_chat[c].push_back(t);
  }
  check_windows(all, limits.global_capacity, limits.global_rate);
  for (const auto& [c, times] : per_chat) {
    check_windows(times, limits.chat_capacity, limits.chat_rate);
  }
}

}  // namespace
}  // namespace sk::maxapi
