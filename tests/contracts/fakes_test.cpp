#include "sertkontrol/fakes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/checked.hpp"

namespace sk::fake {
namespace {

using sk::test::checked;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date kToday = year{2026} / month{9} / day{26};

TEST(FakeCanon, CanonicalizesAsciiInput) {
  EXPECT_EQ(canonicalize("еаэс n RU D-CR.PA08.B.89369/26"), "RUD-CR.PA08.B.89369/26");
  EXPECT_EQ(canonicalize("ru c-ru.ab12.b.00017/24"), "RUC-RU.AB12.B.00017/24");
  EXPECT_EQ(canonicalize("просто текст"), std::nullopt);
}

TEST(FakeCanon, ParsesSerialAndYear) {
  const auto n = parse("RUD-CR.PA08.B.89369/26");
  ASSERT_TRUE(n);
  EXPECT_EQ(checked(n).serial, "89369");
  EXPECT_EQ(checked(n).year, 26);
  EXPECT_EQ(checked(n).kind, canon::DocKind::kDeclaration);
  EXPECT_EQ(checked(parse("RUC-RU.AB12.B.00017/24")).kind, canon::DocKind::kCertificate);
}

TEST(FakeCanon, RejectsMalformed) {
  for (const char* bad : {"", "RU", "XXD-A.B.1/26", "RUD-A.B.1/2", "RUD-A.B.X1/26", "RUD-A.B./26",
                          "RUD-A.B.1/2X", "RUD-AB1-26"}) {
    EXPECT_EQ(parse(bad), std::nullopt) << bad;
  }
}

TEST(FakeCanon, KeyHashIsStableAndDistinguishes) {
  EXPECT_EQ(key_hash("RUD-A"), key_hash("RUD-A"));
  EXPECT_NE(key_hash("RUD-A"), key_hash("RUD-B"));
  EXPECT_EQ(key_hash(""), 14695981039346656037ULL);  // FNV-1a offset basis
}

TEST(FakeCanon, FindNumbersSplitsAndLimits) {
  const auto found = find_numbers("RU D-A.B.1/26\n мусор ,RU C-A.B.2/25; ru d-a.b.3/24", 20);
  EXPECT_EQ(found, (std::vector<std::string>{"RU D-A.B.1/26", "RU C-A.B.2/25", "ru d-a.b.3/24"}));
  EXPECT_EQ(find_numbers("RU 1\nRU 2\nRU 3", 2).size(), 2U);
  EXPECT_TRUE(find_numbers("", 20).empty());
}

TEST(FakeSnapshot, KeysSortedAndMatchRecords) {
  const auto snap = FakeSnapshot::three_records();
  ASSERT_EQ(snap->size(), 3U);
  const auto keys = snap->keys();
  EXPECT_TRUE(std::ranges::is_sorted(keys));
  for (std::size_t i = 0; i < snap->size(); ++i) {
    EXPECT_EQ(keys[i], key_hash(snap->record(i).number));
  }
  EXPECT_TRUE(snap->meta().is_demo);
  EXPECT_EQ(snap->meta().canon_version, canon::kVersion);
  EXPECT_THROW((void)snap->record(3), std::out_of_range);
}

TEST(FakeSnapshot, BySerial) {
  const auto snap = FakeSnapshot::three_records();
  const auto idx = snap->by_serial("89369", 26);
  ASSERT_EQ(idx.size(), 1U);
  EXPECT_EQ(snap->record(idx[0]).number, "RUD-CR.PA08.B.89369/26");
  EXPECT_TRUE(snap->by_serial("89369", 25).empty());
}

bool has_basis(const verify::Verdict& v, verify::Basis b) {
  return std::ranges::any_of(v.findings, [&](const verify::Finding& f) { return f.basis == b; });
}

TEST(FakeCheck, ActiveDocumentIsOkWithFact) {
  const auto snap = FakeSnapshot::three_records();
  const auto v = check(*snap, {.text = "RU D-CR.PA08.B.89369/26", .today = kToday});
  EXPECT_EQ(v.level, verify::Level::kOk);
  ASSERT_TRUE(v.card);
  EXPECT_EQ(checked(v.card).status, snapshot::Status::kActive);
  EXPECT_EQ(checked(v.card).applicant_inn, "7700000016");
  EXPECT_TRUE(has_basis(v, verify::Basis::kFact));
  EXPECT_TRUE(v.is_demo);
  EXPECT_EQ(v.data_date, kToday);
}

TEST(FakeCheck, TerminatedIsProblemWithRecommendation) {
  const auto snap = FakeSnapshot::three_records();
  const auto v = check(*snap, {.text = "RUC-RU.AB12.B.00017/24", .today = kToday});
  EXPECT_EQ(v.level, verify::Level::kProblem);
  EXPECT_TRUE(has_basis(v, verify::Basis::kRecommendation));
}

TEST(FakeCheck, ExpiredIsProblemWithCalculation) {
  const auto snap = FakeSnapshot::three_records();
  const auto v = check(*snap, {.text = "RUD-RU.XY01.A.12345/21", .today = kToday});
  EXPECT_EQ(v.level, verify::Level::kProblem);
  EXPECT_TRUE(has_basis(v, verify::Basis::kCalculation));
}

TEST(FakeCheck, NotFoundMentionsDataDate) {
  const auto snap = FakeSnapshot::three_records();
  const auto v = check(*snap, {.text = "RUD-XX.0000.A.99999/26", .today = kToday});
  EXPECT_EQ(v.level, verify::Level::kNotFound);
  EXPECT_FALSE(v.card);
  ASSERT_FALSE(v.findings.empty());
  EXPECT_EQ(v.findings[0].text, "Нет в данных на 26.09.2026");
}

TEST(FakeSnapshot, ByRegistryId) {
  const auto snap = FakeSnapshot::three_records();
  const auto i = snap->by_registry_id(2);
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(snap->record(sk::test::checked(i)).number, "RUC-RU.AB12.B.00017/24");
  EXPECT_FALSE(snap->by_registry_id(0).has_value());
  EXPECT_FALSE(snap->by_registry_id(99).has_value());
}

TEST(FakeCheck, UnparsedNumber) {
  const auto snap = FakeSnapshot::three_records();
  const auto v = check(*snap, {.text = "привет", .today = kToday});
  EXPECT_EQ(v.level, verify::Level::kNotFound);
  EXPECT_TRUE(v.number.empty());
}

/// Снапшот, где у записи тот же ключ, что у запроса, но другой номер — имитация коллизии XXH3.
class CollidingSnapshot final : public snapshot::Snapshot {
 public:
  explicit CollidingSnapshot(std::uint64_t key) : keys_{key} {}
  [[nodiscard]] const snapshot::SnapshotMeta& meta() const noexcept override { return meta_; }
  [[nodiscard]] std::size_t size() const noexcept override { return 1; }
  [[nodiscard]] std::span<const std::uint64_t> keys() const noexcept override { return keys_; }
  [[nodiscard]] snapshot::RecordView record(std::size_t /*index*/) const override {
    return {.number = "RUD-OTHER.1/26"};
  }
  [[nodiscard]] std::vector<std::uint32_t> by_serial(std::string_view /*serial*/,
                                                     std::uint8_t /*year*/) const override {
    return {};
  }
  [[nodiscard]] std::optional<std::uint32_t> by_registry_id(std::uint64_t /*registry_id*/) const override {
    return std::nullopt;
  }

 private:
  std::vector<std::uint64_t> keys_;
  snapshot::SnapshotMeta meta_{};
};

TEST(FakeCheck, HashCollisionComparesFullString) {
  const CollidingSnapshot snap{key_hash("RUD-A.B.1/26")};
  const auto v = check(snap, {.text = "RUD-A.B.1/26", .today = kToday});
  EXPECT_EQ(v.level, verify::Level::kNotFound);
}

std::vector<std::byte> bytes_of(std::string_view s) {
  std::vector<std::byte> out(s.size());
  std::ranges::transform(s, out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

TEST(FakeRecognize, FindsNumberInTextLayer) {
  const auto file = bytes_of("%PDF\nЕАЭС N RU D-CR.PA08.B.89369/26\n");
  const auto r = fake::recognize(file, recog::MediaType::kPdf, {});
  ASSERT_TRUE(r);
  ASSERT_EQ(r.value().size(), 1U);
  EXPECT_EQ(r.value()[0].source, recog::Source::kTextLayer);
}

TEST(FakeRecognize, RejectsLargeFile) {
  const auto file = bytes_of("RU D-A.B.1/26");
  const auto r = fake::recognize(file, recog::MediaType::kPdf, {.max_bytes = 4});
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::kFileTooLarge);
}

TEST(FakeRecognize, NoNumber) {
  const auto file = bytes_of("картинка без номера");
  const auto r = fake::recognize(file, recog::MediaType::kPng, {});
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::kNumberNotRecognized);
}

TEST(FormatDate, PadsDayAndMonth) {
  EXPECT_EQ(format_date(year{2026} / month{1} / day{5}), "05.01.2026");
}

}  // namespace
}  // namespace sk::fake
