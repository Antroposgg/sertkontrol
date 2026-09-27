/// Diff снапшотов (АРХ §7.3): точечные сценарии и property-тест против наивного `std::map`.
#include "sertkontrol/snapshot/diff.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "sertkontrol/snapshot/lookup.hpp"
#include "sertkontrol/snapshot/writer.hpp"
#include "support/checked.hpp"

namespace sk::snapshot {
namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date kDate = year{2026} / month{9} / day{26};

class DiffTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("sk-diff-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  SnapshotPtr open(std::vector<RecordInput> recs) {
    const auto path = dir_ / ("snap-" + std::to_string(++counter_) + ".bin");
    const auto w =
        write_snapshot(path, std::move(recs), {.version = counter_, .source = "t", .source_date = kDate});
    EXPECT_TRUE(w.has_value()) << (w ? "" : w.error().detail);
    auto r = open_snapshot(path);
    EXPECT_TRUE(r.has_value()) << (r ? "" : r.error().detail);
    return std::move(r).value();
  }

  static std::vector<DocChange> collect(const Snapshot& a, const Snapshot& b, DiffStats* stats = nullptr) {
    std::vector<DocChange> out;
    const auto s = diff(a, b, [&out](const DocChange& c) { out.push_back(c); });
    if (stats != nullptr) {
      *stats = s;
    }
    return out;
  }

 private:
  std::filesystem::path dir_;
  std::uint64_t counter_{0};
};

// find_index: точное совпадение строки после equal_range по XXH3; чужой или пустой номер — nullopt.
TEST_F(DiffTest, FindIndexByCanonicalNumber) {
  const auto snap = open({{.number = "RUD-CR.PA08.B.89369/26", .status = Status::kActive},
                          {.number = "RUD-RU.PA01.B.10001/25", .status = Status::kTerminated}});
  const auto idx = find_index(*snap, "RUD-RU.PA01.B.10001/25");
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(snap->record(idx.value_or(0)).status, Status::kTerminated);
  EXPECT_FALSE(find_index(*snap, "RUD-RU.PA01.B.10001/2").has_value());
  EXPECT_FALSE(find_index(*snap, "").has_value());
  EXPECT_FALSE(find_index(*open({}), "RUD-CR.PA08.B.89369/26").has_value());
}

TEST_F(DiffTest, EmptySnapshots) {
  const auto a = open({});
  const auto b = open({});
  DiffStats stats;
  EXPECT_TRUE(collect(*a, *b, &stats).empty());
  EXPECT_EQ(stats.added + stats.removed + stats.changed + stats.unchanged, 0U);
}

TEST_F(DiffTest, ClassifiesEveryKindOfChange) {
  const auto a = open({
      {.number = "RUD-CR.PA08.B.89369/26",
       .status = Status::kActive,
       .status_date = year{2026} / month{2} / day{10}},
      {.number = "RUD-CN.PA01.B.10002/25",
       .status = Status::kActive,
       .expiry_date = year{2026} / month{10} / day{20}},
      {.number = "RUD-RU.PA04.B.10008/20", .status = Status::kArchived},
      {.number = "RUC-RU.AЯ46.B.10005/24", .status = Status::kTerminated},
  });
  const auto b = open({
      {.number = "RUD-CR.PA08.B.89369/26",
       .status = Status::kSuspended,
       .status_date = year{2026} / month{9} / day{26}},  // статус
      {.number = "RUD-CN.PA01.B.10002/25",
       .status = Status::kActive,
       .expiry_date = year{2031} / month{10} / day{19}},                    // только срок
      {.number = "RUC-RU.AЯ46.B.10005/24", .status = Status::kTerminated},  // без изменений
      {.number = "RUD-CR.PA07.B.89369/26", .status = Status::kActive},      // появился
      // RUD-RU.PA04.B.10008/20 исчез
  });
  DiffStats stats;
  const auto changes = collect(*a, *b, &stats);
  EXPECT_EQ(stats.added, 1U);
  EXPECT_EQ(stats.removed, 1U);
  EXPECT_EQ(stats.changed, 2U);
  EXPECT_EQ(stats.unchanged, 1U);
  ASSERT_EQ(changes.size(), 4U);
  std::map<std::string, DocChange> by_key;
  for (const auto& c : changes) {
    by_key[c.doc_key] = c;
  }
  const auto& suspended = by_key.at("RUD-CR.PA08.B.89369/26");
  EXPECT_EQ(test::checked(suspended.before).status, Status::kActive);
  EXPECT_EQ(test::checked(suspended.after).status, Status::kSuspended);
  EXPECT_EQ(test::checked(suspended.after).status_date, (year{2026} / month{9} / day{26}));
  EXPECT_EQ(test::checked(by_key.at("RUD-CN.PA01.B.10002/25").after).expiry_date,
            (year{2031} / month{10} / day{19}));
  EXPECT_FALSE(by_key.at("RUD-CR.PA07.B.89369/26").before.has_value());
  EXPECT_FALSE(by_key.at("RUD-RU.PA04.B.10008/20").after.has_value());
  // Переход статуса учтён только у совпавших ключей.
  ASSERT_EQ(stats.transitions.size(), 1U);
  EXPECT_EQ((stats.transitions.at({Status::kActive, Status::kSuspended})), 1U);
  // Порядок выдачи — порядок снапшота (хэш, строка).
  for (std::size_t k = 1; k < changes.size(); ++k) {
    EXPECT_LT((std::pair{canon::key_hash(changes[k - 1].doc_key), changes[k - 1].doc_key}),
              (std::pair{canon::key_hash(changes[k].doc_key), changes[k].doc_key}));
  }
}

TEST_F(DiffTest, EmptySinkCountsOnly) {
  const auto a = open({{.number = "RUD-CR.PA08.B.89369/26", .status = Status::kActive}});
  const auto b = open({});
  const auto stats = diff(*a, *b, {});
  EXPECT_EQ(stats.removed, 1U);
}

/// Property: diff совпадает с наивным сравнением двух `std::map` на случайных парах снапшотов.
TEST_F(DiffTest, MatchesNaiveMapOnRandomPairs) {
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): фиксированное зерно — воспроизводимый property-тест.
  std::mt19937 rng{20260927};
  constexpr int kIterations = 150;
  constexpr int kUniverse = 60;  // номера берутся из общего пула — чтобы были и совпадения, и различия
  const auto number = [](int i) { return "RUD-CN.PA01.B." + std::to_string(10000 + i) + "/25"; };
  std::uniform_int_distribution<int> coin{0, 2};
  std::uniform_int_distribution<int> status{0, 5};
  std::uniform_int_distribution<int> date{0, 3};
  for (int it = 0; it < kIterations; ++it) {
    std::map<std::string, DocState> ma;
    std::map<std::string, DocState> mb;
    std::vector<RecordInput> ra;
    std::vector<RecordInput> rb;
    for (int i = 0; i < kUniverse; ++i) {
      const auto gen = [&](std::map<std::string, DocState>& m, std::vector<RecordInput>& r) {
        if (coin(rng) == 0) {
          return;  // записи нет
        }
        DocState s{.status = static_cast<Status>(status(rng))};
        if (const int d = date(rng); d > 0) {
          s.expiry_date = kDate + std::chrono::days{d};
        }
        m[number(i)] = s;
        r.push_back({.number = number(i), .status = s.status, .expiry_date = s.expiry_date});
      };
      gen(ma, ra);
      gen(mb, rb);
    }
    const auto a = open(std::move(ra));
    const auto b = open(std::move(rb));
    std::map<std::string, DocChange> got;
    const auto stats = diff(*a, *b, [&got](const DocChange& c) { got[c.doc_key] = c; });

    std::map<std::string, DocChange> want;
    std::size_t unchanged = 0;
    for (const auto& [k, s] : ma) {
      const auto it_b = mb.find(k);
      if (it_b == mb.end()) {
        want[k] = {.doc_key = k, .before = s};
      } else if (!(it_b->second == s)) {
        want[k] = {.doc_key = k, .before = s, .after = it_b->second};
      } else {
        ++unchanged;
      }
    }
    for (const auto& [k, s] : mb) {
      if (!ma.contains(k)) {
        want[k] = {.doc_key = k, .after = s};
      }
    }
    ASSERT_EQ(got.size(), want.size()) << "итерация " << it;
    EXPECT_EQ(stats.unchanged, unchanged);
    EXPECT_EQ(stats.added + stats.removed + stats.changed, want.size());
    for (const auto& [k, w] : want) {
      const auto& g = got.at(k);
      EXPECT_EQ(g.before, w.before) << k;
      EXPECT_EQ(g.after, w.after) << k;
    }
  }
}

}  // namespace
}  // namespace sk::snapshot
