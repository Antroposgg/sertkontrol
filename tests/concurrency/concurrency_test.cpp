/// Конкурентность под ThreadSanitizer (АРХ §7.4, §10): замена снапшота под нагрузкой читателей и лимит
/// проверок. Без Drogon и libpq — их библиотеки не инструментированы и дали бы ложные срабатывания.
#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

#include "rate_limiter.hpp"
#include "sertkontrol/snapshot/writer.hpp"
#include "sertkontrol/verify/fuzzy.hpp"
#include "snapshot_set.hpp"
#include "support/pg.hpp"

namespace sk::certd {
namespace {

using snapshot::Status;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date kToday = year{2026} / month{9} / day{29};
constexpr std::uint64_t kVersions = 4;

/// Статус документа в версии v — по нему читатель проверяет, что вердикт посчитан на одном снапшоте целиком.
Status status_of(std::uint64_t v) {
  return v % 2 == 0 ? Status::kActive : Status::kSuspended;
}

class SnapshotSwap : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    dir() = std::filesystem::temp_directory_path() / ("sk-tsan-" + std::to_string(test::unique_id()));
    std::filesystem::create_directories(dir());
    for (std::uint64_t v = 1; v <= kVersions; ++v) {
      std::vector<snapshot::RecordInput> records{
          {.number = "RUD-CR.PA08.B.89369/26", .status = status_of(v)},
          {.number = "RUD-CR.PA09.B.89369/26", .status = Status::kActive}};
      for (int i = 0; i < 500; ++i) {
        records.push_back(
            {.number = "RUD-RU.PA01.B." + std::to_string(10000 + i) + "/25", .status = Status::kActive});
      }
      if (!snapshot::write_snapshot(file(v), records,
                                    {.version = v, .source = "tsan", .source_date = kToday})) {
        return;  // ready() == false — тест упадёт в SetUp, а не будет пропущен
      }
    }
    ready() = true;
  }
  // Без ASSERT в SetUpTestSuite: их провал gtest печатает как SKIPPED, и ctest засчитывает тест как
  // пройденный.
  void SetUp() override { ASSERT_TRUE(ready()) << "снапшоты фикстуры не записаны"; }
  static bool& ready() {
    static bool v = false;
    return v;
  }
  static void TearDownTestSuite() { std::filesystem::remove_all(dir()); }
  static std::filesystem::path& dir() {
    static std::filesystem::path v;
    return v;
  }
  static std::filesystem::path file(std::uint64_t v) {
    return dir() / ("snap-" + std::to_string(v) + ".bin");
  }
};

// Обработчик берёт снапшот один раз на запрос (АРХ §7.4): вердикт целиком из одной версии, старое отображение
// освобождается последним читателем, гонок нет.
TEST_F(SnapshotSwap, ReadersSeeConsistentVersionsWhileWriterSwaps) {
  SnapshotSet set;
  set.set(SnapshotRole::kDemoBase, snapshot::open_snapshot(file(1)).value());
  std::atomic<bool> stop{false};
  std::atomic<std::size_t> reads{0};
  std::atomic<std::size_t> errors{0};
  std::vector<std::thread> readers;
  readers.reserve(6);
  for (int t = 0; t < 6; ++t) {
    readers.emplace_back([&, t] {
      while (!stop.load(std::memory_order_relaxed)) {
        const auto snap = set.for_user(true, t % 2 == 0 ? DemoStage::kBase : DemoStage::kUpdated);
        if (!snap) {
          errors.fetch_add(1);
          continue;
        }
        const auto exact = verify::check(*snap, {.text = "RU D-CR.PA08.B.89369/26", .today = kToday});
        const auto fuzzy = verify::check(*snap, {.text = "RU D-CR.PA08.B.89368/26", .today = kToday});
        const auto version = snap->meta().version;
        if (exact.snapshot_version != version || !exact.card || exact.card->status != status_of(version) ||
            fuzzy.level != verify::Level::kNeedsConfirmation) {
          errors.fetch_add(1);
        }
        reads.fetch_add(1);
      }
    });
  }
  // Писатель: каждый раз открывает файл заново (новый mmap), старые отображения уходят с последним читателем.
  for (int i = 0; i < 2000; ++i) {
    const auto v = 1 + (static_cast<std::uint64_t>(i) % kVersions);
    set.set(SnapshotRole::kDemoBase, snapshot::open_snapshot(file(v)).value());
    if (i % 3 == 0) {
      set.set(SnapshotRole::kDemoUpdated,
              i % 2 == 0 ? snapshot::open_snapshot(file(kVersions)).value() : nullptr);
    }
  }
  stop = true;
  for (auto& t : readers) {
    t.join();
  }
  EXPECT_EQ(errors.load(), 0U);
  EXPECT_GT(reads.load(), 100U);
}

TEST(RateLimiterConcurrency, ExactlyCapacityUnderContention) {
  RateLimiter limiter;  // 30 в минуту
  const auto now = RateLimiter::Clock::time_point{};
  std::atomic<int> granted{0};
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 20; ++i) {
        if (limiter.try_acquire(42, now)) {
          granted.fetch_add(1);
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(granted.load(), 30);
}

}  // namespace
}  // namespace sk::certd
