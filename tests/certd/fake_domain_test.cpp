#include "fake_domain.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "sertkontrol/fakes.hpp"

namespace sk::certd {
namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date kToday = year{2026} / month{9} / day{26};
constexpr UserContext kAlice{.max_user_id = 1};
constexpr UserContext kBob{.max_user_id = 2};

class FakeDomainTest : public ::testing::Test {
 protected:
  static snapshot::SnapshotPtr updated_snapshot() {
    std::vector<fake::FakeRecord> recs{
        {.number = "RUD-CR.PA08.B.89369/26", .status = snapshot::Status::kTerminated}};
    return std::make_shared<const fake::FakeSnapshot>(
        std::move(recs),
        snapshot::SnapshotMeta{.version = 2, .source = "fake", .source_date = kToday, .is_demo = true});
  }

  FakeDomainService svc{fake::FakeSnapshot::three_records(), updated_snapshot(), kToday};
};

TEST_F(FakeDomainTest, CheckTextReturnsVerdictPerNumber) {
  const auto r =
      drogon::sync_wait(svc.check_text(kAlice, "RU D-CR.PA08.B.89369/26\nRU C-RU.AB12.B.00017/24"));
  ASSERT_TRUE(r);
  ASSERT_EQ(r.value().verdicts.size(), 2U);
  EXPECT_EQ(r.value().verdicts[0].verdict.level, verify::Level::kOk);
  EXPECT_EQ(r.value().verdicts[1].verdict.level, verify::Level::kProblem);
}

TEST_F(FakeDomainTest, CheckTextWithoutNumbers) {
  const auto r = drogon::sync_wait(svc.check_text(kAlice, "привет"));
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::kNumberNotRecognized);
}

TEST_F(FakeDomainTest, CheckTextLimitsTo20Numbers) {
  std::string text;
  for (int i = 0; i < 25; ++i) {
    text += "RU D-A.B." + std::to_string(i) + "/26\n";
  }
  const auto r = drogon::sync_wait(svc.check_text(kAlice, text));
  ASSERT_TRUE(r);
  EXPECT_EQ(r.value().verdicts.size(), FakeDomainService::kMaxNumbersPerMessage);
}

TEST_F(FakeDomainTest, CheckFile) {
  const std::string pdf = "%PDF RU D-CR.PA08.B.89369/26";
  FileUpload file{.bytes = std::vector<std::byte>(pdf.size()), .type = recog::MediaType::kPdf};
  std::ranges::transform(pdf, file.bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
  const auto r = drogon::sync_wait(svc.check_file(kAlice, file));
  ASSERT_TRUE(r);
  ASSERT_EQ(r.value().verdicts.size(), 1U);
  EXPECT_EQ(r.value().verdicts[0].verdict.level, verify::Level::kOk);

  const auto bad = drogon::sync_wait(svc.check_file(kAlice, FileUpload{}));
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error().code, ErrorCode::kNumberNotRecognized);
}

TEST_F(FakeDomainTest, PortfolioAddListRemove) {
  const auto added = drogon::sync_wait(svc.add_to_portfolio(
      kAlice, {.number = "RU D-CR.PA08.B.89369/26", .sku = "SKU-1", .supplier_inn = "7700000016"}));
  ASSERT_TRUE(added);
  EXPECT_EQ(added.value().item.last_status, snapshot::Status::kActive);
  EXPECT_EQ(added.value().verdict.level, verify::Level::kOk);

  const auto dup =
      drogon::sync_wait(svc.add_to_portfolio(kAlice, {.number = "RUD-CR.PA08.B.89369/26", .sku = "SKU-1"}));
  ASSERT_FALSE(dup);
  EXPECT_EQ(dup.error().code, ErrorCode::kConflict);

  // Тот же номер без SKU — другая запись.
  ASSERT_TRUE(drogon::sync_wait(svc.add_to_portfolio(kAlice, {.number = "RUD-CR.PA08.B.89369/26"})));

  const auto me = drogon::sync_wait(svc.me(kAlice));
  ASSERT_TRUE(me);
  EXPECT_EQ(me.value().portfolio_count, 2U);

  const auto list = drogon::sync_wait(svc.list_portfolio(kAlice, {}));
  ASSERT_TRUE(list);
  EXPECT_EQ(list.value().items.size(), 2U);

  const auto id = added.value().item.id;
  ASSERT_TRUE(drogon::sync_wait(svc.remove_from_portfolio(kAlice, id)));
  const auto again = drogon::sync_wait(svc.remove_from_portfolio(kAlice, id));
  ASSERT_FALSE(again);
  EXPECT_EQ(again.error().code, ErrorCode::kNotFound);
}

TEST_F(FakeDomainTest, AddUnparsedNumber) {
  const auto r = drogon::sync_wait(svc.add_to_portfolio(kAlice, {.number = "не номер"}));
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::kNumberNotRecognized);
}

TEST_F(FakeDomainTest, ForeignItemIsNotFound) {
  const auto added = drogon::sync_wait(svc.add_to_portfolio(kAlice, {.number = "RUD-CR.PA08.B.89369/26"}));
  ASSERT_TRUE(added);
  const auto r = drogon::sync_wait(svc.remove_from_portfolio(kBob, added.value().item.id));
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
  EXPECT_TRUE(drogon::sync_wait(svc.list_portfolio(kBob, {})).value().items.empty());
}

TEST_F(FakeDomainTest, ListFiltersAndPaginates) {
  ASSERT_TRUE(drogon::sync_wait(
      svc.add_to_portfolio(kAlice, {.number = "RUD-CR.PA08.B.89369/26", .supplier_inn = "1"})));
  ASSERT_TRUE(drogon::sync_wait(
      svc.add_to_portfolio(kAlice, {.number = "RUC-RU.AB12.B.00017/24", .supplier_inn = "2"})));
  ASSERT_TRUE(drogon::sync_wait(
      svc.add_to_portfolio(kAlice, {.number = "RUD-RU.XY01.A.12345/21", .supplier_inn = "1"})));

  const auto terminated =
      drogon::sync_wait(svc.list_portfolio(kAlice, {.status = snapshot::Status::kTerminated}));
  ASSERT_EQ(terminated.value().items.size(), 1U);
  EXPECT_EQ(terminated.value().items[0].doc_key, "RUC-RU.AB12.B.00017/24");

  const auto by_inn = drogon::sync_wait(svc.list_portfolio(kAlice, {.supplier_inn = "1"}));
  EXPECT_EQ(by_inn.value().items.size(), 2U);

  const auto first = drogon::sync_wait(svc.list_portfolio(kAlice, {.limit = 2}));
  ASSERT_EQ(first.value().items.size(), 2U);
  ASSERT_TRUE(first.value().next_cursor);
  const auto second =
      drogon::sync_wait(svc.list_portfolio(kAlice, {.cursor = first.value().next_cursor, .limit = 2}));
  ASSERT_EQ(second.value().items.size(), 1U);
  EXPECT_FALSE(second.value().next_cursor);
}

TEST_F(FakeDomainTest, DemoStageSwitchesSnapshotPerUser) {
  EXPECT_EQ(drogon::sync_wait(svc.data_status(kAlice)).value().version, 1U);
  ASSERT_TRUE(drogon::sync_wait(svc.simulate_update(kAlice)));
  EXPECT_EQ(drogon::sync_wait(svc.data_status(kAlice)).value().version, 2U);
  EXPECT_EQ(drogon::sync_wait(svc.me(kAlice)).value().demo_stage, DemoStage::kUpdated);
  // Другой пользователь не затронут.
  EXPECT_EQ(drogon::sync_wait(svc.data_status(kBob)).value().version, 1U);

  const auto v = drogon::sync_wait(svc.check_text(kAlice, "RUD-CR.PA08.B.89369/26"));
  EXPECT_EQ(v.value().verdicts[0].verdict.level, verify::Level::kProblem);

  ASSERT_TRUE(drogon::sync_wait(svc.reset_demo(kAlice)));
  const auto status = drogon::sync_wait(svc.data_status(kAlice)).value();
  EXPECT_EQ(status.version, 1U);
  EXPECT_EQ(status.record_count, 3U);
  EXPECT_TRUE(status.is_demo);
}

}  // namespace
}  // namespace sk::certd
