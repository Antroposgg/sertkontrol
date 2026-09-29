#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "sertkontrol/snapshot/writer.hpp"
#include "sertkontrol/verify/text.hpp"
#include "sertkontrol_contracts.hpp"
#include "support/checked.hpp"
#include "support/files.hpp"

namespace sk::verify {
namespace {

using snapshot::RecordInput;
using snapshot::Status;
using std::chrono::day;
using std::chrono::days;
using std::chrono::month;
using std::chrono::year;

constexpr Date kToday = year{2026} / month{9} / day{26};

std::vector<RecordInput> records() {
  return {
      {.number = "RUD-CR.PA08.B.89369/26",
       .status = Status::kActive,
       .issue_date = year{2026} / month{2} / day{10},
       .expiry_date = year{2031} / month{2} / day{9},
       .applicant_name = "ООО «ТЕСТ»",
       .applicant_inn = "7700000016",
       .manufacturer_name = "ТЕСТ-ЗАВОД",
       .product = "Чайники",
       .tnved = "8516790000",
       .registry_id = 21950326},
      {.number = "RUD-RU.PA01.B.10001/25", .status = Status::kActive, .expiry_date = kToday + days{10}},
      {.number = "RUD-RU.PA01.B.10002/25", .status = Status::kActive, .expiry_date = kToday},
      {.number = "RUD-RU.PA01.B.10003/21",
       .status = Status::kActive,
       .issue_date = year{2021} / month{1} / day{1},
       .expiry_date = year{2024} / month{1} / day{1}},
      {.number = "RUD-RU.PA01.B.10004/25",
       .status = Status::kSuspended,
       .status_date = year{2026} / month{8} / day{1},
       .suspended_until = year{2026} / month{12} / day{1}},
      {.number = "RUC-RU.AЯ46.B.10005/24",
       .status = Status::kTerminated,
       .status_date = year{2026} / month{6} / day{15}},
      {.number = "RUD-RU.PA01.B.10006/25", .status = Status::kAnnulled},
      {.number = "RUD-RU.PA01.B.10007/25", .status = Status::kArchived},
      {.number = "RUD-RU.PA01.B.10008/25", .status = Status::kUnknown},
      {.number = "RUD-RU.PA01.B.10009/25", .status = Status::kActive},
      // Та же серия/год, что у искомых в тестах «нет в данных».
      {.number = "RUD-RU.PA02.B.20000/25", .status = Status::kActive},
      {.number = "RUD-RU.PA03.B.20000/25", .status = Status::kActive},
      {.number = "RUD-RU.XX99.B.20000/25", .status = Status::kActive},
      {.number = "RUD-RU.PA09.B.20000/25", .status = Status::kActive},
  };
}

class RulesTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    snapshot_path = std::filesystem::temp_directory_path() / "sk-verify-rules-test.bin";
    if (!snapshot::write_snapshot(snapshot_path, records(),
                                  {.version = 7, .source = "test", .source_date = kToday, .is_demo = true})) {
      return;  // shared_snapshot == nullptr — тесты упадут в SetUp, а не будут пропущены
    }
    if (auto r = snapshot::open_snapshot(snapshot_path)) {
      shared_snapshot = std::move(r).value();
    }
  }
  // Без ASSERT в SetUpTestSuite: их провал gtest печатает как SKIPPED, и ctest засчитывает тест как
  // пройденный.
  void SetUp() override {
    ASSERT_NE(shared_snapshot, nullptr) << "снапшот фикстуры не записан или не открыт";
  }
  static void TearDownTestSuite() {
    shared_snapshot.reset();
    std::filesystem::remove(snapshot_path);
  }

  static Verdict run(const std::string& number) {
    return check(*shared_snapshot, {.text = number, .today = kToday});
  }

  static std::vector<std::string> rules(const Verdict& v) {
    std::vector<std::string> out;
    out.reserve(v.findings.size());
    for (const auto& f : v.findings) {
      out.push_back(f.rule);
    }
    return out;
  }

  static const Finding* find(const Verdict& v, const std::string& rule) {
    const auto it = std::ranges::find(v.findings, rule, &Finding::rule);
    return it == v.findings.end() ? nullptr : &*it;
  }

  static inline std::filesystem::path
      snapshot_path;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
  static inline snapshot::SnapshotPtr
      shared_snapshot;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};

struct Row {
  std::string number;
  Level level;
  std::vector<std::string> rules;
};

TEST_F(RulesTest, Table) {
  const std::vector<Row> table = {
      {"ЕАЭС N RU Д-CR.РА08.В.89369/26",
       Level::kOk,
       {"status.active", "term.period", "term.remaining", "advice.watch"}},
      {"RU D-RU.PA01.B.10001/25",
       Level::kWarning,
       {"status.active", "term.period", "term.expiring", "advice.renew", "advice.watch"}},
      {"RU D-RU.PA01.B.10002/25",
       Level::kWarning,
       {"status.active", "term.period", "term.expiring", "advice.renew", "advice.watch"}},
      {"RU D-RU.PA01.B.10003/21",
       Level::kProblem,
       {"status.active", "term.period", "term.expired", "advice.replace"}},
      {"RU D-RU.PA01.B.10004/25", Level::kProblem, {"status.suspended", "term.unknown", "advice.replace"}},
      {"RU C-RU.AЯ46.B.10005/24", Level::kProblem, {"status.terminated", "term.unknown", "advice.replace"}},
      {"RU D-RU.PA01.B.10006/25", Level::kProblem, {"status.annulled", "term.unknown", "advice.replace"}},
      {"RU D-RU.PA01.B.10007/25", Level::kProblem, {"status.archived", "term.unknown", "advice.replace"}},
      {"RU D-RU.PA01.B.10008/25",
       Level::kWarning,
       {"status.unknown", "term.unknown", "advice.check_status", "advice.watch"}},
      {"RU D-RU.PA01.B.10009/25", Level::kOk, {"status.active", "term.unknown", "advice.watch"}},
      {"RU D-RU.PA05.B.20000/25", Level::kNotFound, {"not_found", "advice.check_number"}},
      {"RU D-RU.PA01.B.99999/25", Level::kNotFound, {"not_found", "advice.check_number"}},
      {"RU D-RU.PA01.B.99999/26", Level::kNotFound, {"not_found", "not_found.recent", "advice.check_number"}},
      {"привет", Level::kNotFound, {"number.unparsed"}},
  };
  for (const auto& row : table) {
    SCOPED_TRACE(row.number);
    const auto v = run(row.number);
    EXPECT_EQ(v.level, row.level);
    EXPECT_EQ(rules(v), row.rules);
    EXPECT_EQ(v.card.has_value(), row.level != Level::kNotFound);
    EXPECT_EQ(v.query, row.number);
    EXPECT_EQ(v.data_date, kToday);
    EXPECT_EQ(v.snapshot_version, 7U);
    EXPECT_TRUE(v.is_demo);
  }
}

// F8: сверка «заявитель = поставщик». Несовпадение — предупреждение (ok → warning), «проблема» остаётся
// проблемой.
TEST_F(RulesTest, SupplierRules) {
  struct Case {
    const char* number;
    std::optional<std::string> supplier;
    Level level;
    std::vector<std::string> rules;
  };
  const std::vector<Case> cases = {
      {"RU D-CR.PA08.B.89369/26",
       "7700000016",
       Level::kOk,
       {"status.active", "term.period", "term.remaining", "supplier.match", "advice.watch"}},
      {"RU D-CR.PA08.B.89369/26",
       "7700000023",
       Level::kWarning,
       {"status.active", "term.period", "term.remaining", "supplier.mismatch", "advice.watch",
        "advice.check_supplier"}},
      {"RU D-RU.PA01.B.10009/25",
       "7700000023",
       Level::kOk,
       {"status.active", "term.unknown", "supplier.unknown", "advice.watch"}},
      {"RU D-RU.PA01.B.10004/25",
       "7700000023",
       Level::kProblem,
       {"status.suspended", "term.unknown", "supplier.unknown", "advice.replace"}},
      {"RU D-CR.PA08.B.89369/26",
       "",
       Level::kOk,
       {"status.active", "term.period", "term.remaining", "advice.watch"}},
      {"RU D-CR.PA08.B.89369/26",
       std::nullopt,
       Level::kOk,
       {"status.active", "term.period", "term.remaining", "advice.watch"}},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(std::string{c.number} + " / " + c.supplier.value_or("—"));
    const auto v = check(*shared_snapshot, {.text = c.number, .today = kToday, .supplier_inn = c.supplier});
    EXPECT_EQ(v.level, c.level);
    EXPECT_EQ(rules(v), c.rules);
  }
  const auto mismatch = check(
      *shared_snapshot, {.text = "RU D-CR.PA08.B.89369/26", .today = kToday, .supplier_inn = "7700000023"});
  const auto* found = find(mismatch, "supplier.mismatch");
  const auto* advice = find(mismatch, "advice.check_supplier");
  ASSERT_NE(found, nullptr);
  ASSERT_NE(advice, nullptr);
  EXPECT_EQ(found->text,
            "Документ оформлен не на поставщика: заявитель — ИНН 7700000016, поставщик — ИНН 7700000023");
  EXPECT_TRUE(found->basis == Basis::kCalculation);
  EXPECT_TRUE(advice->basis == Basis::kRecommendation);
}

TEST_F(RulesTest, EveryFindingHasBasisMatchingCatalogPrefix) {
  for (const auto* number : {"RU D-CR.PA08.B.89369/26", "RU D-RU.PA01.B.10003/21", "RU D-RU.PA05.B.20000/25",
                             "RU D-RU.PA01.B.99999/26"}) {
    for (const auto& f : run(number).findings) {
      if (f.rule.starts_with("advice.")) {
        EXPECT_EQ(f.basis, Basis::kRecommendation) << f.rule;
      } else if (f.rule.starts_with("status.") || f.rule == "term.period" || f.rule == "term.unknown" ||
                 f.rule == "not_found") {
        EXPECT_EQ(f.basis, Basis::kFact) << f.rule;
      } else {
        EXPECT_EQ(f.basis, Basis::kCalculation) << f.rule;
      }
    }
  }
}

TEST_F(RulesTest, CardFieldsAndRegistryLink) {
  const auto v = run("еаэс n ru дcr.ра08.в.89369/26");
  ASSERT_TRUE(v.card.has_value());
  const auto& c = *v.card;  // NOLINT(bugprone-unchecked-optional-access): проверено выше.
  EXPECT_EQ(v.number, "RUD-CR.PA08.B.89369/26");
  EXPECT_EQ(c.kind, canon::DocKind::kDeclaration);
  EXPECT_EQ(c.applicant_name, "ООО «ТЕСТ»");
  EXPECT_EQ(c.applicant_inn, "7700000016");
  EXPECT_EQ(c.manufacturer_name, "ТЕСТ-ЗАВОД");
  EXPECT_EQ(c.product, "Чайники");
  EXPECT_EQ(c.tnved, "8516790000");
  // Снапшот фикстуры — демо: ссылка на страницу поиска, хотя у записи есть ID (поля демо вымышлены).
  EXPECT_EQ(c.registry_url, "https://pub.fsa.gov.ru/rds/declaration");
  EXPECT_EQ(sk::test::checked(run("RU C-RU.AЯ46.B.10005/24").card).registry_url,
            "https://pub.fsa.gov.ru/rss/certificate");
}

// Боевой снапшот: ссылка ведёт на запись реестра по её ID.
TEST(RegistryLink, ProdSnapshotLinksToRecord) {
  const auto path = std::filesystem::temp_directory_path() / "sk-verify-prod-link.bin";
  ASSERT_TRUE(snapshot::write_snapshot(
      path, {{.number = "RUD-CR.PA08.B.89369/26", .status = Status::kActive, .registry_id = 21950326}},
      {.version = 1, .source = "fsa", .source_date = kToday}));
  const auto snap = snapshot::open_snapshot(path);
  ASSERT_TRUE(snap.has_value());
  const auto v = check(*snap.value(), {.text = "RU D-CR.PA08.B.89369/26", .today = kToday});
  EXPECT_EQ(sk::test::checked(v.card).registry_url,
            "https://pub.fsa.gov.ru/rds/declaration/view/21950326/common");
  std::filesystem::remove(path);
}

TEST_F(RulesTest, TextsMentionDates) {
  EXPECT_EQ(find(run("RU D-RU.PA01.B.10004/25"), "status.suspended")->text,
            "Статус в реестре: приостановлен до 01.12.2026 (с 01.08.2026)");
  EXPECT_EQ(find(run("RU D-RU.PA01.B.10003/21"), "term.expired")->text,
            "Срок действия истёк 01.01.2024 — 999 дн. назад");
  EXPECT_EQ(find(run("RU D-RU.PA01.B.10001/25"), "term.expiring")->text,
            "Срок действия истекает через 10 дн.");
  EXPECT_EQ(find(run("RU D-RU.PA05.B.20000/25"), "not_found")->text,
            "Номера нет в данных реестра на 26.09.2026");
  EXPECT_EQ(
      find(run("RU D-RU.PA01.B.99999/26"), "not_found.recent")->text,
      "Номер 2026 года: документ мог быть зарегистрирован после 26.09.2026 — проверьте его по ссылке из "
      "QR-кода выписки");
  EXPECT_EQ(find(run("RU D-RU.PA01.B.99999/25"), "not_found.recent"), nullptr);
}

TEST_F(RulesTest, NotFoundSuggestsNearestOfSameSerial) {
  const auto v = run("RU D-RU.PA05.B.20000/25");
  ASSERT_EQ(v.suggestions.size(), kMaxSuggestions);
  EXPECT_EQ(v.suggestions[0].number, "RUD-RU.PA02.B.20000/25");
  EXPECT_EQ(v.suggestions[0].distance, 1.0);
  EXPECT_EQ(v.suggestions[1].number, "RUD-RU.PA03.B.20000/25");
  EXPECT_EQ(v.suggestions[2].number, "RUD-RU.PA09.B.20000/25");
  EXPECT_TRUE(std::ranges::is_sorted(v.suggestions, {}, &Suggestion::distance));
  EXPECT_TRUE(run("RU D-RU.PA01.B.99999/25").suggestions.empty());
  EXPECT_TRUE(run("RU D-RU.AB12.B.00001").suggestions.empty());  // нет года — нет серии
}

TEST_F(RulesTest, EveryRuleIsDocumentedInCatalog) {
  const auto md = sk::test::read_file(SK_RULES_MD);
  ASSERT_FALSE(md.empty());
  std::set<std::string> documented;
  const std::regex id{R"(\|\s*`([a-z_]+\.?[a-z_]*)`\s*\|)"};
  for (std::sregex_iterator it{md.begin(), md.end(), id}, end; it != end; ++it) {
    documented.insert((*it)[1]);
  }
  std::set<std::string> used;
  for (const auto* n : {"RU D-CR.PA08.B.89369/26", "RU D-RU.PA01.B.10001/25", "RU D-RU.PA01.B.10003/21",
                        "RU D-RU.PA01.B.10004/25", "RU C-RU.AЯ46.B.10005/24", "RU D-RU.PA01.B.10006/25",
                        "RU D-RU.PA01.B.10007/25", "RU D-RU.PA01.B.10008/25", "RU D-RU.PA01.B.10009/25",
                        "RU D-RU.PA05.B.20000/25", "RU D-RU.PA01.B.1000Z/25", "x"}) {
    for (const auto& f : run(n).findings) {
      used.insert(f.rule);
    }
  }
  for (const auto* inn : {"7700000016", "7700000023"}) {
    for (const auto* n : {"RU D-CR.PA08.B.89369/26", "RU D-RU.PA01.B.10009/25"}) {
      for (const auto& f :
           check(*shared_snapshot, {.text = n, .today = kToday, .supplier_inn = inn}).findings) {
        used.insert(f.rule);
      }
    }
  }
  for (const auto& r : used) {
    EXPECT_TRUE(documented.contains(r)) << "правило «" << r << "» не описано в docs/rules.md";
  }
}

}  // namespace
}  // namespace sk::verify
