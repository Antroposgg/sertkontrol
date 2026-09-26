/// Тесты БД-слоя certd на настоящем PostgreSQL 16 (scripts/ci/with-pg.sh). Без SK_TEST_PG — пропускаются.
#include "support/pg.hpp"

#include <drogon/orm/DbClient.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "domain_service.hpp"
#include "pg_ports.hpp"
#include "sertkontrol/maxapi/fake_bot_api.hpp"
#include "sertkontrol/snapshot/writer.hpp"
#include "snapshot_loader.hpp"
#include "support/files.hpp"

namespace sk::certd {
namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date kToday = year{2026} / month{9} / day{26};

std::vector<std::byte> bytes_of(const std::string& s) {
  std::vector<std::byte> out(s.size());
  std::ranges::transform(s, out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

/// Общая среда: БД, демо-снапшот из data/demo (через ingest-формат не нужен — пишем напрямую).
class PgEnv : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    const auto pg = test::test_pg();
    if (!pg) {
      return;
    }
    db() = drogon::orm::DbClient::newPgClient(*pg, 2);
    dir() = std::filesystem::temp_directory_path() / ("sk-certd-pg-" + std::to_string(test::unique_id()));
    std::filesystem::create_directories(dir());
    version() = static_cast<std::uint64_t>(test::unique_id());
    const std::vector<snapshot::RecordInput> records{
        {.number = "RUD-CR.PA08.B.89369/26",
         .status = snapshot::Status::kActive,
         .expiry_date = year{2031} / month{2} / day{9},
         .applicant_name = "ООО «ТЕСТ»",
         .applicant_inn = "7700000016"},
        {.number = "RUC-RU.AЯ46.B.10005/24", .status = snapshot::Status::kTerminated},
        {.number = "RUD-RU.PA01.B.10001/25", .status = snapshot::Status::kActive},
    };
    ASSERT_TRUE(snapshot::write_snapshot(
        dir() / "snap.bin", records,
        {.version = version(), .source = "test", .source_date = kToday, .is_demo = true}));
    holder().set(snapshot::open_snapshot(dir() / "snap.bin").value());
  }
  static void TearDownTestSuite() {
    holder().set(nullptr);
    if (!dir().empty()) {
      std::filesystem::remove_all(dir());
    }
  }

  void SetUp() override {
    if (!db()) {
      GTEST_SKIP() << "SK_TEST_PG не задан: запускайте через scripts/ci/with-pg.sh";
    }
  }

  // Состояние набора — функции со статическими локальными, а не глобальные переменные.
  static drogon::orm::DbClientPtr& db() {
    static drogon::orm::DbClientPtr v;
    return v;
  }
  static std::filesystem::path& dir() {
    static std::filesystem::path v;
    return v;
  }
  static std::uint64_t& version() {
    static std::uint64_t v = 0;
    return v;
  }
  static snapshot::SnapshotHolder& holder() {
    static snapshot::SnapshotHolder v;
    return v;
  }
};

class DomainPgTest : public PgEnv {
 public:
  RateLimiter limiter{30};
  RecognitionPool pool{1, 4, recog::recognize};
  std::unique_ptr<DomainServiceImpl> svc;
  UserContext alice{.max_user_id = test::unique_id(), .channel = Channel::kBot};
  UserContext bob{.max_user_id = test::unique_id(), .channel = Channel::kApp};

  void SetUp() override {
    PgEnv::SetUp();
    if (!IsSkipped()) {
      svc = std::make_unique<DomainServiceImpl>(db(), holder(), pool, limiter, [] { return kToday; });
    }
  }

  template <class T>
  static T run(drogon::Task<T> t) {
    return drogon::sync_wait(std::move(t));
  }
};

// F1 (точный поиск) и F3: номер в любой раскладке, несколько номеров в сообщении, журнал проверок.
TEST_F(DomainPgTest, CheckTextLogsEveryNumber) {
  const auto r =
      run(svc->check_text(alice, "еаэс n ru дcr.ра08.в.89369/26, RU C-RU.АЯ46.В.10005/24 и мусор"));
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  ASSERT_EQ(r.value().verdicts.size(), 2U);
  EXPECT_EQ(r.value().verdicts[0].verdict.level, verify::Level::kOk);
  EXPECT_EQ(r.value().verdicts[1].verdict.level, verify::Level::kProblem);
  EXPECT_EQ(r.value().batch_id, r.value().verdicts[0].check_id);
  const auto log = db()->execSqlSync(
      "SELECT count(*) AS n, min(via) AS via FROM check_log WHERE batch_id = $1", r.value().batch_id);
  EXPECT_EQ(log[0]["n"].as<int>(), 2);
  EXPECT_EQ(log[0]["via"].as<std::string>(), "bot_text");
  EXPECT_EQ(run(svc->check_text(alice, "без номера")).error().code, ErrorCode::kNumberNotRecognized);
}

// F2 через домен: PDF → пул распознавания → вердикт со ссылкой из QR.
TEST_F(DomainPgTest, CheckFileUsesQrLink) {
  const auto pdf = test::read_file(std::filesystem::path{SK_FIXTURES_DIR} / "pdf" / "extract-89369-26.pdf");
  const auto r =
      run(svc->check_file(bob, FileUpload{.bytes = bytes_of(pdf), .type = recog::MediaType::kPdf}));
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  ASSERT_EQ(r.value().verdicts.size(), 1U);
  const auto& v = r.value().verdicts[0].verdict;
  EXPECT_EQ(v.number, "RUD-CR.PA08.B.89369/26");
  ASSERT_TRUE(v.card.has_value());
  EXPECT_EQ(v.card.value_or(verify::Card{}).registry_url,
            "https://pub.fsa.gov.ru/rds/declaration/view/21950326/common");
}

// F4: на контроль, список с фильтрами и курсором, удалить; конфликт; ИНН.
TEST_F(DomainPgTest, PortfolioLifecycle) {
  auto a = run(svc->add_to_portfolio(
      alice, {.number = "RU D-CR.PA08.B.89369/26", .sku = "SKU-1", .supplier_inn = "7700000016"}));
  ASSERT_TRUE(a.has_value()) << a.error().detail;
  EXPECT_EQ(a.value().item.last_status, snapshot::Status::kActive);
  EXPECT_EQ(a.value().item.last_version, version());
  EXPECT_EQ(
      run(svc->add_to_portfolio(alice, {.number = "RUD-CR.PA08.B.89369/26", .sku = "SKU-1"})).error().code,
      ErrorCode::kConflict);
  ASSERT_TRUE(
      run(svc->add_to_portfolio(alice, {.number = "RUD-CR.PA08.B.89369/26"})));  // без SKU — другая запись
  EXPECT_EQ(run(svc->add_to_portfolio(alice, {.number = "RUD-CR.PA08.B.89369/26"})).error().code,
            ErrorCode::kConflict);  // NULLS NOT DISTINCT
  ASSERT_TRUE(run(svc->add_to_portfolio(alice, {.number = "RUC-RU.AЯ46.B.10005/24"})));
  EXPECT_EQ(run(svc->add_to_portfolio(alice, {.number = "RU D-A.B.1/26", .supplier_inn = "7700000017"}))
                .error()
                .code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(run(svc->add_to_portfolio(alice, {.number = "мусор"})).error().code,
            ErrorCode::kNumberNotRecognized);

  EXPECT_EQ(run(svc->me(alice)).value().portfolio_count, 3U);
  const auto all = run(svc->list_portfolio(alice, {.limit = 2}));
  ASSERT_EQ(all.value().items.size(), 2U);
  ASSERT_TRUE(all.value().next_cursor.has_value());
  const auto rest = run(svc->list_portfolio(alice, {.cursor = all.value().next_cursor, .limit = 2}));
  EXPECT_EQ(rest.value().items.size(), 1U);
  EXPECT_FALSE(rest.value().next_cursor.has_value());
  const auto terminated = run(svc->list_portfolio(alice, {.status = snapshot::Status::kTerminated}));
  ASSERT_EQ(terminated.value().items.size(), 1U);
  EXPECT_EQ(terminated.value().items[0].doc_kind, canon::DocKind::kCertificate);
  const auto by_inn = run(svc->list_portfolio(alice, {.supplier_inn = "7700000016"}));
  ASSERT_EQ(by_inn.value().items.size(), 1U);
  EXPECT_EQ(by_inn.value().items[0].sku, "SKU-1");

  ASSERT_TRUE(run(svc->remove_from_portfolio(alice, a.value().item.id)));
  EXPECT_EQ(run(svc->remove_from_portfolio(alice, a.value().item.id)).error().code, ErrorCode::kNotFound);
}

// АРХ §10: чужой id → 404, чужие записи не видны.
TEST_F(DomainPgTest, IdorForeignIdIsNotFound) {
  const auto a = run(svc->add_to_portfolio(alice, {.number = "RU D-CR.PA08.B.89369/26"}));
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(run(svc->remove_from_portfolio(bob, a.value().item.id)).error().code, ErrorCode::kNotFound);
  EXPECT_TRUE(run(svc->list_portfolio(bob, {})).value().items.empty());
  const auto checked = run(svc->check_text(alice, "RU D-RU.PA01.B.10001/25"));
  EXPECT_EQ(run(svc->add_checked(bob, checked.value().verdicts[0].check_id)).error().code,
            ErrorCode::kNotFound);
  EXPECT_EQ(run(svc->add_batch(bob, checked.value().batch_id)).error().code, ErrorCode::kNotFound);
  EXPECT_EQ(run(svc->me(alice)).value().portfolio_count, 1U);
}

TEST_F(DomainPgTest, AddFromChecksAndBatch) {
  const auto checked = run(svc->check_text(alice, "RU D-CR.PA08.B.89369/26 RU D-RU.PA01.B.10001/25 привет"));
  ASSERT_TRUE(checked.has_value());
  const auto one = run(svc->add_checked(alice, checked.value().verdicts[0].check_id));
  ASSERT_TRUE(one.has_value()) << one.error().detail;
  EXPECT_EQ(one.value().item.doc_key, "RUD-CR.PA08.B.89369/26");
  const auto batch = run(svc->add_batch(alice, checked.value().batch_id));
  ASSERT_TRUE(batch.has_value());
  EXPECT_EQ(batch.value().added, 1U);
  EXPECT_EQ(batch.value().already, 1U);
  EXPECT_EQ(run(svc->add_checked(alice, 0)).error().code, ErrorCode::kNotFound);
}

TEST_F(DomainPgTest, ConsentDataStatusAndDemoStubs) {
  EXPECT_FALSE(run(svc->me(alice)).value().consented);
  ASSERT_TRUE(run(svc->give_consent(alice)));
  ASSERT_TRUE(run(svc->give_consent(alice)));
  const auto me = run(svc->me(alice)).value();
  EXPECT_TRUE(me.consented);
  EXPECT_TRUE(me.is_demo);
  EXPECT_EQ(me.demo_stage, DemoStage::kBase);
  const auto ds = run(svc->data_status(alice)).value();
  EXPECT_EQ(ds.version, version());
  EXPECT_EQ(ds.record_count, 3U);
  EXPECT_EQ(ds.source_date, kToday);
  EXPECT_EQ(run(svc->simulate_update(alice)).error().code, ErrorCode::kForbidden);
  EXPECT_EQ(run(svc->reset_demo(alice)).error().code, ErrorCode::kForbidden);
}

TEST_F(DomainPgTest, RateLimitAndMissingSnapshot) {
  RateLimiter tight{2};
  const snapshot::SnapshotHolder empty;
  DomainServiceImpl limited{db(), holder(), pool, tight, [] { return kToday; }};
  ASSERT_TRUE(run(limited.check_text(alice, "RU D-A.B.1/26")));
  ASSERT_TRUE(run(limited.check_text(alice, "RU D-A.B.1/26")));
  EXPECT_EQ(run(limited.check_text(alice, "RU D-A.B.1/26")).error().code, ErrorCode::kRateLimited);
  EXPECT_EQ(run(limited.check_file(alice, {})).error().code, ErrorCode::kRateLimited);
  DomainServiceImpl no_data{db(), empty, pool, limiter, [] { return kToday; }};
  EXPECT_EQ(run(no_data.check_text(bob, "RU D-A.B.1/26")).error().code, ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(no_data.add_to_portfolio(bob, {.number = "RU D-A.B.1/26"})).error().code,
            ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(no_data.data_status(bob)).error().code, ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(svc->check_file(
                    bob, FileUpload{.bytes = bytes_of("%PDF-1.4 битый"), .type = recog::MediaType::kPdf}))
                .error()
                .code,
            ErrorCode::kUnsupportedMediaType);
}

TEST_F(DomainPgTest, DatabaseErrorsBecomeInternal) {
  const auto dead =
      drogon::orm::DbClient::newPgClient("host=127.0.0.1 port=1 dbname=x user=x connect_timeout=1", 1);
  dead->setTimeout(2.0);
  DomainServiceImpl broken{dead, holder(), pool, limiter, [] { return kToday; }};
  EXPECT_EQ(run(broken.me(alice)).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(broken.check_text(alice, "RU D-A.B.1/26")).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(broken.list_portfolio(alice, {})).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(broken.add_checked(alice, 1)).error().code, ErrorCode::kInternal);
}

// ── Порты бота и отправитель outbox ──

class PortsPgTest : public PgEnv {};

TEST_F(PortsPgTest, InboundLogDeduplicates) {
  PgInboundLog log{db()};
  const auto key = "m:test-" + std::to_string(test::unique_id());
  EXPECT_TRUE(drogon::sync_wait(log.first_seen(key)).value());
  EXPECT_FALSE(drogon::sync_wait(log.first_seen(key)).value());
  drogon::sync_wait(log.mark_processed(key, "ошибка"));
  const auto r = db()->execSqlSync(
      "SELECT error, processed_at IS NOT NULL AS done FROM inbound_update "
      "WHERE dedup_key = $1",
      key);
  EXPECT_EQ(r[0]["error"].as<std::string>(), "ошибка");
  EXPECT_TRUE(r[0]["done"].as<bool>());
}

TEST_F(PortsPgTest, OutboxSendRetryAndFail) {
  // Изолируем очередь: снимаем всё чужое из pending, чтобы отправитель видел только сообщения теста.
  db()->execSqlSync("UPDATE outbox SET status = 'failed' WHERE status IN ('pending', 'sending')");
  PgOutbox outbox{db()};
  maxapi::RecordingBotApi api;
  OutboxSender sender{db(), api};
  const auto user = test::unique_id();
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "низкий"}, 0)));
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "высокий"}, 10)));
  EXPECT_EQ(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = ""}, 0)).error().code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(drogon::sync_wait(sender.drain(10)).value(), 2U);
  ASSERT_EQ(api.messages().size(), 2U);
  EXPECT_EQ(api.messages()[0].text, "высокий");  // приоритет раньше порядка

  api.fail_next_send();
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "повтор"}, 0)));
  EXPECT_EQ(drogon::sync_wait(sender.drain(10)).value(), 1U);
  const auto pending = db()->execSqlSync(
      "SELECT status, attempts, last_error FROM outbox WHERE payload->>'text' = 'повтор' ORDER BY id DESC "
      "LIMIT 1");
  EXPECT_EQ(pending[0]["status"].as<std::string>(), "pending");
  EXPECT_EQ(pending[0]["attempts"].as<int>(), 1);
  // Досрочно разрешаем повтор и отправляем.
  db()->execSqlSync("UPDATE outbox SET not_before = now() WHERE payload->>'text' = 'повтор'");
  EXPECT_EQ(drogon::sync_wait(sender.drain(10)).value(), 1U);
  EXPECT_EQ(api.messages().back().text, "повтор");

  // Невосстановимая ошибка — сразу failed.
  db()->execSqlSync(
      "INSERT INTO outbox (user_id, payload) SELECT id, '{\"version\":2}'::jsonb FROM app_user WHERE "
      "max_user_id = $1",
      user);
  EXPECT_EQ(drogon::sync_wait(sender.drain(10)).value(), 1U);
  const auto failed =
      db()->execSqlSync("SELECT status FROM outbox WHERE payload->>'version' = '2' ORDER BY id DESC LIMIT 1");
  EXPECT_EQ(failed[0]["status"].as<std::string>(), "failed");
  EXPECT_TRUE(drogon::sync_wait(sender.recover_stale()).has_value());
}

TEST_F(PortsPgTest, SnapshotLoaderPicksLatestReady) {
  snapshot::SnapshotHolder h;
  SnapshotLoader loader{db(), h};
  const auto v = static_cast<std::uint64_t>(test::unique_id()) + 1'000'000'000'000ULL;
  const auto path = dir() / "loader.bin";
  ASSERT_TRUE(snapshot::write_snapshot(path, {}, {.version = v, .source = "test", .source_date = kToday}));
  db()->execSqlSync(
      "INSERT INTO snapshot_version (version, source, source_date, file_path, status) VALUES ($1, 'test', "
      "$2, $3, "
      "'ready')",
      static_cast<std::int64_t>(v), std::string{"2026-09-26"}, path.string());
  EXPECT_TRUE(drogon::sync_wait(loader.refresh()).value());
  EXPECT_EQ(h.get()->meta().version, v);
  EXPECT_FALSE(drogon::sync_wait(loader.refresh()).value());  // без изменений
  // Новая версия с битым путём: ошибка, старый снапшот остаётся (АРХ §4).
  db()->execSqlSync(
      "INSERT INTO snapshot_version (version, source, source_date, file_path, status) VALUES ($1, 'test', "
      "$2, "
      "'/nonexistent', 'ready')",
      static_cast<std::int64_t>(v + 1), std::string{"2026-09-26"});
  EXPECT_FALSE(drogon::sync_wait(loader.refresh()).has_value());
  EXPECT_EQ(h.get()->meta().version, v);
  db()->execSqlSync("DELETE FROM snapshot_version WHERE version IN ($1, $2)", static_cast<std::int64_t>(v),
                    static_cast<std::int64_t>(v + 1));
}

}  // namespace
}  // namespace sk::certd
