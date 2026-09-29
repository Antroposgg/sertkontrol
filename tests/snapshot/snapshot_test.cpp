#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <xxhash.h>

#include "sertkontrol/fakes.hpp"
#include "sertkontrol/snapshot/format.hpp"
#include "sertkontrol/snapshot/holder.hpp"
#include "sertkontrol/snapshot/writer.hpp"
#include "support/checked.hpp"
#include "support/files.hpp"

namespace sk::snapshot {
namespace {

namespace fmt = format;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date kDate = year{2026} / month{9} / day{26};

class SnapshotTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("sk-snapshot-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }

  static std::vector<RecordInput> sample() {
    return {
        {.number = "RUD-CR.PA08.B.89369/26",
         .status = Status::kActive,
         .issue_date = year{2026} / month{2} / day{10},
         .expiry_date = year{2031} / month{2} / day{9},
         .status_date = year{2026} / month{2} / day{10},
         .applicant_name = "ООО «ТЕСТ»",
         .applicant_inn = "7700000016",
         .manufacturer_name = "ТЕСТ",
         .product = "Чайники электрические",
         .tnved = "8516790000",
         .country = "RU",
         .lab_accreditation = "RA.RU.21AA01",
         .registry_id = 21950326},
        {.number = "RUC-RU.AЯ46.B.00017/24", .status = Status::kTerminated, .applicant_name = "ООО «ТЕСТ»"},
        {.number = "RUD-RU.AБ12.B.00001", .status = Status::kArchived},
        {.number = "RUD-CB.PAO8.B.89369/26",
         .status = Status::kSuspended,
         .suspended_until = year{2026} / month{12} / day{31}},
    };
  }

  std::filesystem::path write(std::vector<RecordInput> recs, const std::string& name = "snap-1.bin") {
    const auto path = dir_ / name;
    const auto r = write_snapshot(path, std::move(recs),
                                  {.version = 1, .source = "demo", .source_date = kDate, .is_demo = true});
    EXPECT_TRUE(r.has_value()) << (r ? "" : r.error().detail);
    return path;
  }

  static std::string read_all(const std::filesystem::path& p) { return sk::test::read_file(p); }
  static void write_all(const std::filesystem::path& p, const std::string& bytes) {
    sk::test::write_file(p, bytes);
  }

  /// Портит файл функцией `mutate` над заголовком и данными и пересчитывает контрольную сумму,
  /// чтобы проверить глубокие проверки ридера, а не только XXH3.
  template <class F>
  static void corrupt_with_valid_checksum(const std::filesystem::path& p, F mutate) {
    auto bytes = read_all(p);
    fmt::FileHeader h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    mutate(h, bytes);
    h.payload_xxh3 = XXH3_64bits(bytes.data() + sizeof(h), bytes.size() - sizeof(h));
    std::memcpy(bytes.data(), &h, sizeof(h));
    write_all(p, bytes);
  }

  static ErrorCode open_error(const std::filesystem::path& p) {
    const auto r = open_snapshot(p);
    EXPECT_FALSE(r.has_value());
    return r ? ErrorCode::kInternal : r.error().code;
  }

 private:
  std::filesystem::path dir_;
};

TEST_F(SnapshotTest, RoundTrip) {
  const auto path = write(sample());
  EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
  const auto r = open_snapshot(path);
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  const auto& snap = *r.value();
  EXPECT_EQ(snap.size(), 4U);
  EXPECT_EQ(snap.meta().version, 1U);
  EXPECT_EQ(snap.meta().source, "demo");
  EXPECT_EQ(snap.meta().source_date, kDate);
  EXPECT_EQ(snap.meta().canon_version, canon::kVersion);
  EXPECT_TRUE(snap.meta().is_demo);

  const auto keys = snap.keys();
  EXPECT_TRUE(std::ranges::is_sorted(keys));
  bool found = false;
  for (std::size_t i = 0; i < snap.size(); ++i) {
    const auto rec = snap.record(i);
    EXPECT_EQ(keys[i], canon::key_hash(rec.number));
    if (rec.number == "RUD-CR.PA08.B.89369/26") {
      found = true;
      EXPECT_EQ(rec.kind, canon::DocKind::kDeclaration);
      EXPECT_EQ(rec.status, Status::kActive);
      EXPECT_EQ(rec.issue_date, (year{2026} / month{2} / day{10}));
      EXPECT_EQ(rec.expiry_date, (year{2031} / month{2} / day{9}));
      EXPECT_FALSE(rec.suspended_until.has_value());
      EXPECT_EQ(rec.applicant_name, "ООО «ТЕСТ»");
      EXPECT_EQ(rec.applicant_inn, "7700000016");
      EXPECT_EQ(rec.product, "Чайники электрические");
      EXPECT_EQ(rec.tnved, "8516790000");
      EXPECT_EQ(rec.country, "RU");
      EXPECT_EQ(rec.lab_accreditation, "RA.RU.21AA01");
      EXPECT_EQ(rec.registry_id, 21950326U);
    }
    if (rec.number == "RUC-RU.AЯ46.B.00017/24") {
      EXPECT_EQ(rec.kind, canon::DocKind::kCertificate);
      EXPECT_TRUE(rec.applicant_inn.empty());
    }
  }
  EXPECT_TRUE(found);
  EXPECT_THROW((void)snap.record(4), std::out_of_range);
}

TEST_F(SnapshotTest, BySerialFindsBothVariantsOfSameSerial) {
  const auto r = open_snapshot(write(sample()));
  ASSERT_TRUE(r.has_value());
  const auto& snap = *r.value();
  auto idx = snap.by_serial("89369", 26);
  ASSERT_EQ(idx.size(), 2U);
  std::vector<std::string> numbers;
  numbers.reserve(idx.size());
  for (const auto i : idx) {
    numbers.emplace_back(snap.record(i).number);
  }
  std::ranges::sort(numbers);
  EXPECT_EQ(numbers, (std::vector<std::string>{"RUD-CB.PAO8.B.89369/26", "RUD-CR.PA08.B.89369/26"}));
  EXPECT_TRUE(snap.by_serial("89369", 25).empty());
  EXPECT_TRUE(snap.by_serial("00001", 0).empty());  // старый формат без года — не в индексе
  EXPECT_TRUE(snap.by_serial("12a", 26).empty());
  EXPECT_TRUE(snap.by_serial("", 26).empty());
}

// C2 by_registry_id: QR выписки несёт ID записи реестра; индекс строится один раз, в том числе при
// одновременных первых обращениях.
TEST_F(SnapshotTest, ByRegistryId) {
  const auto r = open_snapshot(write(sample()));
  ASSERT_TRUE(r.has_value());
  const auto& snap = *r.value();
  std::array<std::optional<std::uint32_t>, 4> found{};
  std::vector<std::thread> readers;
  readers.reserve(found.size());
  for (auto& slot : found) {
    readers.emplace_back([&snap, &slot] { slot = snap.by_registry_id(21950326); });
  }
  for (auto& t : readers) {
    t.join();
  }
  for (const auto& f : found) {
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(snap.record(sk::test::checked(f)).number, "RUD-CR.PA08.B.89369/26");
  }
  EXPECT_FALSE(snap.by_registry_id(0).has_value());  // «неизвестен» — не ключ поиска
  EXPECT_FALSE(snap.by_registry_id(1).has_value());
  EXPECT_FALSE(snap.by_registry_id(UINT64_MAX).has_value());
  const auto empty = open_snapshot(write({}, "empty-registry.bin"));
  ASSERT_TRUE(empty.has_value());
  EXPECT_FALSE(empty.value()->by_registry_id(21950326).has_value());
}

TEST_F(SnapshotTest, EmptySnapshot) {
  const auto r = open_snapshot(write({}));
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value()->size(), 0U);
  EXPECT_TRUE(r.value()->keys().empty());
}

TEST_F(SnapshotTest, Deterministic) {
  auto reversed = sample();
  std::ranges::reverse(reversed);
  EXPECT_EQ(read_all(write(sample(), "a.bin")), read_all(write(reversed, "b.bin")));
}

TEST_F(SnapshotTest, DuplicatesKeepLatestStatusDate) {
  auto recs = sample();
  recs.push_back({.number = "RUD-CR.PA08.B.89369/26",
                  .status = Status::kTerminated,
                  .status_date = year{2026} / month{9} / day{1}});
  recs.push_back({.number = "RUD-CR.PA08.B.89369/26",
                  .status = Status::kSuspended,
                  .status_date = year{2026} / month{3} / day{1}});
  const auto path = dir() / "dup.bin";
  const auto stats = write_snapshot(path, recs, {.version = 2, .source = "demo", .source_date = kDate});
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats.value().records, 4U);
  EXPECT_EQ(stats.value().duplicates, 2U);
  const auto snap = open_snapshot(path).value();
  for (std::size_t i = 0; i < snap->size(); ++i) {
    if (snap->record(i).number == "RUD-CR.PA08.B.89369/26") {
      EXPECT_EQ(snap->record(i).status, Status::kTerminated);
    }
  }
  EXPECT_FALSE(snap->meta().is_demo);
}

TEST_F(SnapshotTest, ProductTruncatedOnUtf8Boundary) {
  std::string product;
  for (int i = 0; i < 100; ++i) {
    product += "я";  // 2 байта
  }
  const auto snap = open_snapshot(write({{.number = "RUD-RU.PA01.B.12345/23", .product = product}})).value();
  const auto p = snap->record(0).product;
  EXPECT_EQ(p.size(), fmt::kMaxProductBytes);
  EXPECT_EQ(p, product.substr(0, fmt::kMaxProductBytes));
}

TEST_F(SnapshotTest, RejectsBadInput) {
  const WriteOptions ok{.version = 1, .source = "demo", .source_date = kDate};
  EXPECT_EQ(write_snapshot(dir() / "x.bin", {{.number = "RU D-RU.PA01.B.1/23"}}, ok).error().code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(write_snapshot(dir() / "x.bin", {{.number = ""}}, ok).error().code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(write_snapshot(dir() / "x.bin", {}, {.source = std::string(40, 'x')}).error().code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(write_snapshot(dir() / "no-such-dir" / "x.bin", {}, ok).error().code, ErrorCode::kInternal);
}

TEST_F(SnapshotTest, OpenMissingAndShortFiles) {
  EXPECT_EQ(open_error(dir() / "missing.bin"), ErrorCode::kSnapshotUnavailable);
  write_all(dir() / "short.bin", "SKSNAP01");
  EXPECT_EQ(open_error(dir() / "short.bin"), ErrorCode::kSnapshotUnavailable);
}

TEST_F(SnapshotTest, DetectsFlippedByte) {
  const auto path = write(sample());
  auto bytes = read_all(path);
  bytes[bytes.size() - 1] = static_cast<char>(bytes.back() ^ 0x01);
  write_all(path, bytes);
  const auto r = open_snapshot(path);
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().detail.find("контрольная сумма"), std::string::npos);
}

TEST_F(SnapshotTest, RejectsWrongHeaderFields) {
  struct Case {
    const char* name;
    void (*mutate)(fmt::FileHeader&, std::string&);
    const char* expect;
  };
  const std::vector<Case> cases = {
      {"magic", [](fmt::FileHeader& h, std::string&) { h.magic[0] = 'X'; }, "сигнатура"},
      {"format", [](fmt::FileHeader& h, std::string&) { h.format_version = 2; }, "версия формата"},
      {"canon", [](fmt::FileHeader& h, std::string&) { h.canon_version = canon::kVersion + 1; },
       "canon::kVersion"},
      {"section-out-of-file", [](fmt::FileHeader& h, std::string&) { h.sections[1].size = 1ULL << 40; },
       "вне файла"},
      {"section-unaligned", [](fmt::FileHeader& h, std::string&) { h.sections[0].offset += 8; },
       "не выровнена"},
      // Находка fuzz: у пустой секции смещение тоже проверяется — `base + offset` вне файла уже UB.
      {"empty-section-offset",
       [](fmt::FileHeader& h, std::string&) {
         h.sections[static_cast<std::size_t>(fmt::Section::kSerialIndex)] = {.offset = 1ULL << 60, .size = 0};
       },
       "вне файла"},
      {"count", [](fmt::FileHeader& h, std::string&) { h.record_count += 1; }, "размеры секций"},
      {"no-strings", [](fmt::FileHeader& h, std::string&) { h.string_count = 0; }, "размеры секций"},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.name);
    const auto path = write(sample(), std::string{c.name} + ".bin");
    corrupt_with_valid_checksum(path, c.mutate);
    const auto r = open_snapshot(path);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::kSnapshotUnavailable);
    EXPECT_NE(r.error().detail.find(c.expect), std::string::npos) << r.error().detail;
  }
}

TEST_F(SnapshotTest, RejectsBadPayloadStructure) {
  const auto at = [](const fmt::FileHeader& h, fmt::Section s) {
    return static_cast<std::size_t>(h.sections.at(static_cast<std::size_t>(s)).offset);
  };
  struct Case {
    const char* name;
    std::function<void(fmt::FileHeader&, std::string&)> mutate;
    const char* expect;
  };
  const std::vector<Case> cases = {
      {"unsorted-keys",
       [&](fmt::FileHeader& h, std::string& b) {
         const auto off = at(h, fmt::Section::kKeys);
         std::uint64_t first = UINT64_MAX;
         std::memcpy(b.data() + off, &first, sizeof(first));
       },
       "не отсортированы"},
      {"string-ref",
       [&](fmt::FileHeader& h, std::string& b) {
         const auto off = at(h, fmt::Section::kRecords) + offsetof(fmt::PackedRecord, product);
         const std::uint32_t bad = 100000;
         std::memcpy(b.data() + off, &bad, sizeof(bad));
       },
       "за пределы данных"},
      {"status", [&](fmt::FileHeader& h, std::string& b) { b[at(h, fmt::Section::kRecords)] = 42; },
       "за пределы данных"},
      {"offsets",
       [&](fmt::FileHeader& h, std::string& b) {
         const std::uint64_t bad = 5;
         std::memcpy(b.data() + at(h, fmt::Section::kStringOffsets), &bad, sizeof(bad));
       },
       "не монотонны"},
      {"offsets-end",
       [&](fmt::FileHeader& h, std::string& b) {
         const auto off = at(h, fmt::Section::kStringOffsets) + (h.string_count * sizeof(std::uint64_t));
         std::uint64_t last = 0;
         std::memcpy(&last, b.data() + off, sizeof(last));
         last += 1;
         std::memcpy(b.data() + off, &last, sizeof(last));
       },
       "не совпадает с данными"},
      {"serial-index",
       [&](fmt::FileHeader& h, std::string& b) {
         const std::uint32_t bad = 1000;
         std::memcpy(b.data() + at(h, fmt::Section::kSerialIndex) + 8, &bad, sizeof(bad));
       },
       "serial_index"},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.name);
    const auto path = write(sample(), std::string{c.name} + ".bin");
    corrupt_with_valid_checksum(path, c.mutate);
    const auto r = open_snapshot(path);
    ASSERT_FALSE(r.has_value());
    EXPECT_NE(r.error().detail.find(c.expect), std::string::npos) << r.error().detail;
  }
}

TEST(SerialKey, Encoding) {
  std::uint64_t k = 0;
  ASSERT_TRUE(fmt::serial_key("89369", 26, k));
  EXPECT_EQ(k, 8936926U);
  ASSERT_TRUE(fmt::serial_key("9999999999", 99, k));
  EXPECT_EQ(k, 999999999999ULL);
  EXPECT_FALSE(fmt::serial_key("12345678901", 1, k));
  EXPECT_FALSE(fmt::serial_key("1", 100, k));
}

TEST(SnapshotHolder, SwapUnderReaders) {
  SnapshotHolder holder;
  EXPECT_EQ(holder.get(), nullptr);
  std::atomic<bool> stop{false};
  std::atomic<std::size_t> reads{0};
  std::vector<std::thread> readers;
  readers.reserve(4);
  for (int t = 0; t < 4; ++t) {
    readers.emplace_back([&] {
      while (!stop.load()) {
        const auto s = holder.get();  // один раз на «запрос»
        if (s) {
          EXPECT_GE(s->size(), 0U);
        }
        reads.fetch_add(1);
      }
    });
  }
  for (int i = 1; i <= 200; ++i) {
    holder.set(i % 2 == 0 ? SnapshotPtr{fake::FakeSnapshot::three_records()} : SnapshotPtr{});
  }
  stop = true;
  for (auto& t : readers) {
    t.join();
  }
  EXPECT_GT(reads.load(), 0U);
  EXPECT_NE(holder.get(), nullptr);
}

}  // namespace
}  // namespace sk::snapshot
