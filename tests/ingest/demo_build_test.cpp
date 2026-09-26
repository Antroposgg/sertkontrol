#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "build.hpp"
#include "demo_source.hpp"
#include "normalize.hpp"
#include "registry_db.hpp"
#include "support/files.hpp"
#include "support/pg.hpp"

namespace sk::ingest {
namespace {

using snapshot::Status;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

std::filesystem::path fixtures_dir() {
  return SK_FIXTURES_DIR;
}
std::filesystem::path demo_dir() {
  return SK_DEMO_DIR;
}

std::filesystem::path temp_dir(const std::string& name) {
  auto d = std::filesystem::temp_directory_path() /
           ("sk-ingest-" + name + "-" + std::to_string(test::unique_id()));
  std::filesystem::create_directories(d);
  return d;
}

TEST(StatusFromSource, KnownAndUnknown) {
  EXPECT_EQ(status_from_source("Действует"), Status::kActive);
  EXPECT_EQ(status_from_source(" ДЕЙСТВУЕТ "), Status::kActive);
  EXPECT_EQ(status_from_source("Возобновлён"), Status::kActive);
  EXPECT_EQ(status_from_source("Приостановлен"), Status::kSuspended);
  EXPECT_EQ(status_from_source("Прекращён"), Status::kTerminated);
  EXPECT_EQ(status_from_source("прекращен"), Status::kTerminated);
  EXPECT_EQ(status_from_source("Аннулирован"), Status::kAnnulled);
  EXPECT_EQ(status_from_source("Архивный"), Status::kArchived);
  EXPECT_EQ(status_from_source("Продлён"), Status::kActive);
  EXPECT_EQ(status_from_source("Частично приостановлен"), Status::kSuspended);
  EXPECT_EQ(status_from_source("Черновик"), std::nullopt);
  EXPECT_EQ(status_from_source(""), std::nullopt);
}

TEST(ParseIsoDate, Cases) {
  EXPECT_TRUE(parse_iso_date("").ok);
  EXPECT_FALSE(parse_iso_date("").date.has_value());
  EXPECT_EQ(parse_iso_date("2026-02-10").date, (year{2026} / month{2} / day{10}));
  for (const char* bad : {"2026-02-30", "2026-13-01", "26-02-10", "2026/02/10", "2026-0a-10", "2026-02-1x"}) {
    EXPECT_FALSE(parse_iso_date(bad).ok) << bad;
  }
}

TEST(Normalize, RecordAndErrors) {
  RawRecord raw{
      .number = "ЕАЭС N RU Д-CR.РА08.В.89369/26", .status = "Действует", .expiry_date = "2031-02-09"};
  const auto ok = normalize(raw);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value().number, "RUD-CR.PA08.B.89369/26");
  EXPECT_EQ(ok.value().expiry_date, (year{2031} / month{2} / day{9}));

  raw.status = "Черновик";
  EXPECT_NE(normalize(raw).error().detail.find("статус"), std::string::npos);
  raw.status = "Действует";
  raw.issue_date = "вчера";
  EXPECT_NE(normalize(raw).error().detail.find("дата"), std::string::npos);
  raw.number = "без номера";
  EXPECT_NE(normalize(raw).error().detail.find("номер"), std::string::npos);
}

TEST(DemoTsvSource, ReadsFixture) {
  auto src = DemoTsvSource::open(fixtures_dir() / "demo-small.tsv");
  ASSERT_TRUE(src.has_value()) << src.error().detail;
  EXPECT_EQ(DemoTsvSource::name(), "demo");
  EXPECT_EQ(src.value().source_date(), (year{2026} / month{9} / day{25}));
  std::vector<RawRecord> rows{};
  const auto n = src.value().for_each([&](RawRecord&& r) { rows.push_back(std::move(r)); });
  ASSERT_TRUE(n.has_value());
  ASSERT_EQ(n.value(), 2U);
  EXPECT_EQ(rows[1].status, "Прекращён");
  EXPECT_EQ(rows[1].registry_id, 42U);
  EXPECT_TRUE(rows[0].suspended_until.empty());
}

TEST(DemoTsvSource, RejectsBrokenFiles) {
  const auto dir = temp_dir("broken");
  const std::string header =
      "number\tstatus\tissue_date\texpiry_date\tstatus_date\tsuspended_until\tapplicant_name\tapplicant_inn\t"
      "manufacturer_name\tproduct\ttnved\tcountry\tlab_accreditation\tregistry_id\n";
  const std::string row = "RU D-A.B.1/23\tДействует\t\t\t\t\t\t\t\t\t\t\t\t0\n";
  struct Case {
    std::string name;
    std::string content;
    bool opens;
  };
  const std::vector<Case> cases = {
      {"no-date", header + row, false},
      {"bad-date", "#source_date=2026-99-01\n" + header, false},
      {"bad-header", "#source_date=2026-09-25\nnumber\tstatus\n", false},
      {"no-header", "#source_date=2026-09-25\n", false},
      {"short-row", "#source_date=2026-09-25\n" + header + "RU D-A.B.1/23\tДействует\n", true},
      {"bad-id", "#source_date=2026-09-25\n" + header + "RU D-A.B.1/23\tДействует\t\t\t\t\t\t\t\t\t\t\t\tx\n",
       true},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.name);
    test::write_file(dir / c.name, c.content);
    auto src = DemoTsvSource::open(dir / c.name);
    ASSERT_EQ(src.has_value(), c.opens);
    if (c.opens) {
      EXPECT_FALSE(src.value().for_each([](const RawRecord&) {}).has_value());
    }
  }
  EXPECT_EQ(DemoTsvSource::open(dir / "missing.tsv").error().code, ErrorCode::kNotFound);
  std::filesystem::remove_all(dir);
}

TEST(BuildSnapshot, DemoBaseMatchesVerdictScenario) {
  auto src = DemoTsvSource::open(demo_dir() / "base.tsv");
  ASSERT_TRUE(src.has_value()) << src.error().detail;
  const auto dir = temp_dir("demo");
  const auto built = build_snapshot(src.value(), dir, 1, true);
  ASSERT_TRUE(built.has_value()) << built.error().detail;
  EXPECT_EQ(built.value().rejected, 0U);
  EXPECT_EQ(built.value().file, dir / "snap-1.bin");
  const auto snap = snapshot::open_snapshot(built.value().file);
  ASSERT_TRUE(snap.has_value());
  EXPECT_EQ(snap.value()->size(), built.value().stats.records);
  EXPECT_TRUE(snap.value()->meta().is_demo);
  EXPECT_EQ(snap.value()->meta().source, "demo");
  // Номер выписки из АРХ есть в демо-данных, у серии 89369/26 два варианта (для «ближайших номеров»).
  EXPECT_EQ(snap.value()->by_serial("89369", 26).size(), 2U);
  // Повторная сборка детерминирована.
  auto again = DemoTsvSource::open(demo_dir() / "base.tsv");
  const auto rebuilt = build_snapshot(again.value(), dir, 1, true);
  ASSERT_TRUE(rebuilt.has_value());
  EXPECT_EQ(rebuilt.value().stats.checksum, built.value().stats.checksum);
  std::filesystem::remove_all(dir);
}

/// Адаптер для проверки quality gate.
struct ListSource {
  std::vector<RawRecord> rows{};
  bool fail{false};
  [[nodiscard]] static std::string_view name() noexcept { return "list"; }
  [[nodiscard]] static Date source_date() noexcept { return year{2026} / month{9} / day{25}; }
  Result<std::size_t> for_each(const RecordSink& sink) {
    if (fail) {
      return Error{ErrorCode::kInternal, "источник недоступен"};
    }
    for (auto r : rows) {
      sink(std::move(r));
    }
    return rows.size();
  }
};
static_assert(SourceAdapter<ListSource>);

TEST(BuildSnapshot, QualityGateAndSourceErrors) {
  const auto dir = temp_dir("gate");
  ListSource bad;
  for (int i = 0; i < 20; ++i) {
    bad.rows.push_back(
        {.number = "RU D-RU.PA01.B." + std::to_string(10000 + i) + "/23", .status = "Действует"});
  }
  bad.rows.push_back({.number = "мусор", .status = "Действует"});  // 1 из 21 < 5%
  EXPECT_TRUE(build_snapshot(bad, dir, 3, false).has_value());
  bad.rows.push_back({.number = "мусор2", .status = "Действует"});  // 2 из 22 > 5%
  const auto gated = build_snapshot(bad, dir, 3, false);
  ASSERT_FALSE(gated.has_value());
  EXPECT_NE(gated.error().detail.find("quality gate"), std::string::npos);
  ListSource broken{.fail = true};
  EXPECT_EQ(build_snapshot(broken, dir, 3, false).error().code, ErrorCode::kInternal);
  EXPECT_FALSE(build_snapshot(bad, dir / "missing", 3, false).has_value());
  std::filesystem::remove_all(dir);
}

TEST(PgConninfo, FromEnvironment) {
  const auto env = [](std::string_view name) -> std::optional<std::string> {
    if (name == "POSTGRES_HOST") {
      return "db";
    }
    if (name == "POSTGRES_PASSWORD") {
      return "p'w";
    }
    return std::nullopt;
  };
  EXPECT_EQ(pg_conninfo(env),
            R"(host='db' port='5432' dbname='sertkontrol' user='sertkontrol' password='p\'w')");
  EXPECT_EQ(pg_conninfo([](std::string_view) { return std::optional<std::string>{}; }),
            "host='postgres' port='5432' dbname='sertkontrol' user='sertkontrol'");
}

TEST(RegistryDb, RegistersVersionLifecycle) {
  const auto pg = test::test_pg();
  if (!pg) {
    GTEST_SKIP() << "SK_TEST_PG не задан: запускайте через scripts/ci/with-pg.sh";
  }
  auto db = RegistryDb::connect(*pg);
  ASSERT_TRUE(db.has_value()) << db.error().detail;
  const auto version = static_cast<std::uint64_t>(test::unique_id());
  const Date date = year{2026} / month{9} / day{25};
  ASSERT_TRUE(db.value().mark_building(version, "demo", date, "/data/snapshots/x.bin", true));
  BuildResult b;
  b.stats.records = 16;
  ASSERT_TRUE(db.value().mark_ready(version, b));
  ASSERT_TRUE(
      db.value().mark_building(version, "demo", date, "/data/snapshots/x.bin", true));  // повтор — сброс
  ASSERT_TRUE(db.value().mark_failed(version, "тест"));
  EXPECT_FALSE(RegistryDb::connect("host=127.0.0.1 port=1 connect_timeout=1").has_value());
}

}  // namespace
}  // namespace sk::ingest
