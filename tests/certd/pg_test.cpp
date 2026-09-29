/// Тесты БД-слоя certd на настоящем PostgreSQL 16 (scripts/ci/with-pg.sh). Без SK_TEST_PG — пропускаются.
#include "support/pg.hpp"

#include <drogon/orm/DbClient.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <tuple>

#include "bot/card.hpp"
#include "domain_service.hpp"
#include "jobs.hpp"
#include "maintenance.hpp"
#include "notify.hpp"
#include "pg_ports.hpp"
#include "sertkontrol/maxapi/fake_bot_api.hpp"
#include "sertkontrol/snapshot/writer.hpp"
#include "snapshot_loader.hpp"
#include "support/checked.hpp"
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

/// Общая среда: БД и демо-пара N / N+1 (пишем снапшоты напрямую, версии регистрируем в snapshot_version).
/// В N+1: 89369/26 приостановлен, 10005/24 без изменений, появился PA07 89369/26 (как в data/demo).
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
    const std::vector<snapshot::RecordInput> base{
        {.number = "RUD-CR.PA08.B.89369/26",
         .status = snapshot::Status::kActive,
         .expiry_date = year{2031} / month{2} / day{9},
         .applicant_name = "ООО «ТЕСТ»",
         .applicant_inn = "7700000016"},
        {.number = "RUC-RU.AЯ46.B.10005/24", .status = snapshot::Status::kTerminated},
        {.number = "RUD-RU.PA01.B.10001/25", .status = snapshot::Status::kActive},
    };
    auto next = base;
    next[0].status = snapshot::Status::kSuspended;
    next[0].status_date = kToday;
    next[0].suspended_until = year{2026} / month{12} / day{26};
    next.push_back({.number = "RUD-CR.PA07.B.89369/26", .status = snapshot::Status::kActive});
    ASSERT_TRUE(snapshot::write_snapshot(
        dir() / "snap.bin", base,
        {.version = version(), .source = "test", .source_date = kToday, .is_demo = true}));
    ASSERT_TRUE(snapshot::write_snapshot(dir() / "snap-next.bin", next,
                                         {.version = version() + 1,
                                          .source = "test",
                                          .source_date = kToday + std::chrono::days{1},
                                          .is_demo = true}));
    set().set(SnapshotRole::kDemoBase, snapshot::open_snapshot(dir() / "snap.bin").value());
    set().set(SnapshotRole::kDemoUpdated, snapshot::open_snapshot(dir() / "snap-next.bin").value());
    // Версии нужны истории (JOIN snapshot_version) и уведомлениям (FK notification.version). Статус building:
    // загрузчики certd в других тестах их не подхватывают.
    for (const auto& [v, stage, date] : {std::tuple{version(), 0, std::string{"2026-09-26"}},
                                         std::tuple{version() + 1, 1, std::string{"2026-09-27"}}}) {
      db()->execSqlSync(
          "INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo, "
          "demo_stage) "
          "VALUES ($1, 'test', $2::date, 'x', 'ready', true, $3::smallint)",
          static_cast<std::int64_t>(v), date, static_cast<std::int16_t>(stage));
    }
    db()->execSqlSync(
        "INSERT INTO registry_change (version, doc_key, before, after, status_date) VALUES "
        "($1, 'RUD-CR.PA08.B.89369/26', "
        "'{\"status\":\"active\",\"expiry_date\":\"2031-02-09\",\"status_date\":null}', "
        "'{\"status\":\"suspended\",\"expiry_date\":\"2031-02-09\",\"status_date\":\"2026-09-26\"}', "
        "'2026-09-26'), "
        "($1, 'RUD-CR.PA07.B.89369/26', NULL, "
        "'{\"status\":\"active\",\"expiry_date\":null,\"status_date\":null}', NULL)",
        static_cast<std::int64_t>(version() + 1));
  }
  static void TearDownTestSuite() {
    set().set(SnapshotRole::kDemoBase, nullptr);
    set().set(SnapshotRole::kDemoUpdated, nullptr);
    if (db()) {
      db()->execSqlSync("DELETE FROM snapshot_version WHERE version IN ($1, $2)",
                        static_cast<std::int64_t>(version()), static_cast<std::int64_t>(version() + 1));
    }
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
  static SnapshotSet& set() {
    static SnapshotSet v;
    return v;
  }
  static NoticeRenderer renderer() {
    return [](const ChangeNotice& n) { return bot::render_change_notice(n, {}); };
  }
};

class DomainPgTest : public PgEnv {
 public:
  RateLimiter limiter{30};
  RecognitionPool pool{1, 4, recog::recognize};
  std::unique_ptr<NotifyService> notify;
  std::unique_ptr<DomainServiceImpl> svc;
  UserContext alice{.max_user_id = test::unique_id(), .channel = Channel::kBot};
  UserContext bob{.max_user_id = test::unique_id(), .channel = Channel::kApp};

  void SetUp() override {
    PgEnv::SetUp();
    if (!IsSkipped()) {
      notify = std::make_unique<NotifyService>(db(), renderer());
      svc = std::make_unique<DomainServiceImpl>(db(), set(), pool, limiter, *notify, [] { return kToday; });
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

// F1: номер с ошибками OCR (АРХ §7.1, третья строка golden) → «Это номер …?» → «Да» → карточка точного
// номера.
TEST_F(DomainPgTest, FuzzyNumberConfirmed) {
  const auto checked = run(svc->check_text(alice, "ЕАЭС М RU ДСВ.РАО8.В.89369/26"));
  ASSERT_TRUE(checked.has_value()) << checked.error().detail;
  const auto& question = checked.value().verdicts.at(0);
  EXPECT_EQ(question.verdict.level, verify::Level::kNeedsConfirmation);
  EXPECT_EQ(question.verdict.suggestions.at(0).number, "RUD-CR.PA08.B.89369/26");
  const auto confirmed = run(svc->confirm(alice, question.check_id));
  ASSERT_TRUE(confirmed.has_value()) << confirmed.error().detail;
  const auto& exact = confirmed.value().verdicts.at(0);
  EXPECT_EQ(exact.verdict.level, verify::Level::kOk);
  EXPECT_EQ(exact.verdict.number, "RUD-CR.PA08.B.89369/26");
  EXPECT_NE(exact.check_id, question.check_id);  // новая строка журнала — на неё работает «На контроль»
  ASSERT_TRUE(run(svc->add_checked(alice, exact.check_id)));
  // Чужая проверка и проверка без уверенной подсказки — kNotFound.
  EXPECT_EQ(run(svc->confirm(bob, question.check_id)).error().code, ErrorCode::kNotFound);
  EXPECT_EQ(run(svc->confirm(alice, exact.check_id)).error().code, ErrorCode::kNotFound);
  EXPECT_EQ(run(svc->confirm(alice, 0)).error().code, ErrorCode::kNotFound);
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

TEST_F(DomainPgTest, ConsentAndDataStatus) {
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
  EXPECT_EQ(ds.demo_stage, DemoStage::kBase);
  EXPECT_TRUE(ds.demo_update_available);
}

// F6 + F5: «Симулировать обновление» → ровно одно уведомление на документ, повтор идемпотентен, сброс
// возвращает сценарий в начало.
TEST_F(DomainPgTest, DemoSimulateNotifiesOnceAndResetRepeats) {
  const auto watched =
      run(svc->add_to_portfolio(alice, {.number = "RU D-CR.PA08.B.89369/26", .sku = "ЧАЙ-1"}));
  ASSERT_TRUE(watched.has_value()) << watched.error().detail;
  ASSERT_TRUE(run(svc->add_to_portfolio(alice, {.number = "RU D-CR.PA07.B.89369/26"})));  // нет в N
  ASSERT_TRUE(run(svc->add_to_portfolio(alice, {.number = "RUC-RU.AЯ46.B.10005/24"})));  // не меняется
  ASSERT_TRUE(run(svc->add_to_portfolio(bob, {.number = "RU D-CR.PA08.B.89369/26"})));  // чужой портфель
  const auto outbox_of = [](const UserContext& u) {
    return db()->execSqlSync(
        "SELECT count(*) AS n, string_agg(o.payload->>'text', '|') AS t FROM outbox o JOIN app_user a ON "
        "a.id = o.user_id WHERE a.max_user_id = $1 AND o.payload->>'kind' = 'status_changed'",
        u.max_user_id)[0];
  };

  const auto first = run(svc->simulate_update(alice));
  ASSERT_TRUE(first.has_value()) << first.error().detail;
  EXPECT_EQ(first.value().notified, 2U);  // приостановлен + появился
  const auto msgs = outbox_of(alice);
  EXPECT_EQ(msgs["n"].as<int>(), 1);  // одно сообщение на пользователя и версию
  EXPECT_NE(msgs["t"].as<std::string>().find("приостановлен"), std::string::npos);
  EXPECT_NE(msgs["t"].as<std::string>().find("SKU ЧАЙ-1"), std::string::npos);
  EXPECT_EQ(outbox_of(bob)["n"].as<int>(), 0);  // уведомляется только нажавший
  const auto list = run(svc->list_portfolio(alice, {.status = snapshot::Status::kSuspended}));
  ASSERT_EQ(list.value().items.size(), 1U);
  EXPECT_EQ(list.value().items[0].last_version, version() + 1);
  const auto ds = run(svc->data_status(alice)).value();
  EXPECT_EQ(ds.version, version() + 1);
  EXPECT_EQ(ds.demo_stage, DemoStage::kUpdated);
  // Проверка теперь идёт по N+1.
  EXPECT_EQ(run(svc->check_text(alice, "RU D-CR.PA08.B.89369/26")).value().verdicts[0].verdict.level,
            verify::Level::kProblem);

  EXPECT_EQ(run(svc->simulate_update(alice)).value().notified, 0U);  // повтор — без дублей
  EXPECT_EQ(outbox_of(alice)["n"].as<int>(), 1);

  ASSERT_TRUE(run(svc->reset_demo(alice)));
  EXPECT_EQ(run(svc->data_status(alice)).value().demo_stage, DemoStage::kBase);
  EXPECT_EQ(run(svc->list_portfolio(alice, {.status = snapshot::Status::kSuspended})).value().items.size(),
            0U);
  EXPECT_EQ(run(svc->list_portfolio(alice, {.status = snapshot::Status::kUnknown})).value().items.size(), 1U);
  EXPECT_EQ(run(svc->simulate_update(alice)).value().notified, 2U);  // сценарий повторяется
  EXPECT_EQ(outbox_of(alice)["n"].as<int>(), 2);
}

TEST_F(DomainPgTest, HistoryFollowsDemoStage) {
  EXPECT_TRUE(run(svc->history(alice, "RU D-CR.PA08.B.89369/26")).value().entries.empty());  // стадия N
  ASSERT_TRUE(run(svc->simulate_update(alice)));
  const auto h = run(svc->history(alice, "ЕАЭС N RU Д-CR.РА08.В.89369/26"));
  ASSERT_TRUE(h.has_value()) << h.error().detail;
  EXPECT_EQ(h.value().doc_key, "RUD-CR.PA08.B.89369/26");
  ASSERT_EQ(h.value().entries.size(), 1U);
  const auto& e = h.value().entries[0];
  EXPECT_EQ(e.version, version() + 1);
  EXPECT_EQ(e.data_date, (year{2026} / month{9} / day{27}));
  ASSERT_TRUE(e.before && e.after);
  EXPECT_EQ(test::checked(e.before).status, snapshot::Status::kActive);
  EXPECT_EQ(test::checked(e.after).status, snapshot::Status::kSuspended);
  EXPECT_EQ(test::checked(e.after).status_date, kToday);
  const auto appeared = run(svc->history(alice, "RU D-CR.PA07.B.89369/26")).value();
  ASSERT_EQ(appeared.entries.size(), 1U);
  EXPECT_FALSE(appeared.entries[0].before.has_value());
  EXPECT_EQ(run(svc->history(alice, "мусор")).error().code, ErrorCode::kNumberNotRecognized);
}

TEST_F(DomainPgTest, AttachSupplierAddsOrUpdates) {
  const auto checked = run(svc->check_text(alice, "RU D-CR.PA08.B.89369/26"));
  const auto id = checked.value().verdicts[0].check_id;
  EXPECT_EQ(run(svc->attach_supplier(alice, id, "7700000017")).error().code, ErrorCode::kInvalidArgument);
  const auto first = run(svc->attach_supplier(alice, id, "7700000016"));
  ASSERT_TRUE(first.has_value()) << first.error().detail;
  EXPECT_EQ(first.value().item.supplier_inn, "7700000016");
  // Второй раз — та же запись с новым поставщиком, а не конфликт.
  const auto second = run(svc->attach_supplier(alice, id, "500100732259"));
  ASSERT_TRUE(second.has_value()) << second.error().detail;
  EXPECT_EQ(second.value().item.id, first.value().item.id);
  EXPECT_EQ(run(svc->me(alice)).value().portfolio_count, 1U);
  EXPECT_EQ(run(svc->list_portfolio(alice, {.supplier_inn = "500100732259"})).value().items.size(), 1U);
  EXPECT_EQ(run(svc->attach_supplier(bob, id, "7700000016")).error().code, ErrorCode::kNotFound);
}

TEST_F(DomainPgTest, DemoForbiddenForProdUsers) {
  ASSERT_TRUE(run(svc->me(bob)));
  db()->execSqlSync("UPDATE app_user SET is_demo = false WHERE max_user_id = $1", bob.max_user_id);
  EXPECT_EQ(run(svc->simulate_update(bob)).error().code, ErrorCode::kForbidden);
  EXPECT_EQ(run(svc->reset_demo(bob)).error().code, ErrorCode::kForbidden);
  // Боевого снапшота нет — демо-данные не подменяют боевые (КЕЙС §2 п.10).
  EXPECT_EQ(run(svc->check_text(bob, "RU D-CR.PA08.B.89369/26")).error().code,
            ErrorCode::kSnapshotUnavailable);
  db()->execSqlSync("DELETE FROM app_user WHERE max_user_id = $1", bob.max_user_id);
}

TEST_F(DomainPgTest, RateLimitAndMissingSnapshot) {
  RateLimiter tight{2};
  const SnapshotSet empty;
  DomainServiceImpl limited{db(), set(), pool, tight, *notify, [] { return kToday; }};
  ASSERT_TRUE(run(limited.check_text(alice, "RU D-A.B.1/26")));
  ASSERT_TRUE(run(limited.check_text(alice, "RU D-A.B.1/26")));
  EXPECT_EQ(run(limited.check_text(alice, "RU D-A.B.1/26")).error().code, ErrorCode::kRateLimited);
  EXPECT_EQ(run(limited.check_file(alice, {})).error().code, ErrorCode::kRateLimited);
  DomainServiceImpl no_data{db(), empty, pool, limiter, *notify, [] { return kToday; }};
  EXPECT_EQ(run(no_data.check_text(bob, "RU D-A.B.1/26")).error().code, ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(no_data.add_to_portfolio(bob, {.number = "RU D-A.B.1/26"})).error().code,
            ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(no_data.data_status(bob)).error().code, ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(no_data.simulate_update(bob)).error().code, ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(no_data.reset_demo(bob)).error().code, ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(no_data.history(bob, "RU D-A.B.1/26")).error().code, ErrorCode::kSnapshotUnavailable);
  EXPECT_EQ(run(svc->check_file(
                    bob, FileUpload{.bytes = bytes_of("%PDF-1.4 битый"), .type = recog::MediaType::kPdf}))
                .error()
                .code,
            ErrorCode::kUnsupportedMediaType);
}

TEST_F(DomainPgTest, DatabaseErrorsBecomeInternal) {
  const auto dead =
      drogon::orm::DbClient::newPgClient("host=127.0.0.1 port=1 dbname=x user=x connect_timeout=1", 1);
  dead->setTimeout(1.0);
  NotifyService dead_notify{dead, renderer()};
  DomainServiceImpl broken{dead, set(), pool, limiter, dead_notify, [] { return kToday; }};
  EXPECT_EQ(run(broken.me(alice)).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(broken.check_text(alice, "RU D-A.B.1/26")).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(broken.list_portfolio(alice, {})).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(broken.add_checked(alice, 1)).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(broken.history(alice, "RU D-A.B.1/26")).error().code, ErrorCode::kInternal);
  EXPECT_EQ(run(dead_notify.notify_version(set().get(SnapshotRole::kDemoBase))).error().code,
            ErrorCode::kInternal);
  EXPECT_EQ(run(dead_notify.notify_version(nullptr)).error().code, ErrorCode::kSnapshotUnavailable);
}

// ── Порты бота и отправитель outbox ──

/// Обработчик задачи для JobQueueLeaseRetryAndExhaustion: считает вызовы и падает, пока `ok == false`.
struct Probe {
  int calls{0};
  bool ok{false};
};

drogon::Task<Result<Ok>> probe_handler(Probe* probe, std::string payload) {
  ++probe->calls;
  EXPECT_EQ(payload, R"({"n": 1})");
  if (!probe->ok) {
    co_return Error{ErrorCode::kInternal, "сбой"};
  }
  co_return Ok{};
}

class PortsPgTest : public PgEnv {
 protected:
  /// Изолируем очередь: снимаем всё чужое из pending, чтобы отправитель видел только сообщения теста.
  static void isolate_outbox() {
    db()->execSqlSync("UPDATE outbox SET status = 'failed' WHERE status IN ('pending', 'sending')");
  }
  static drogon::orm::Row outbox_row(const std::string& text) {
    return db()->execSqlSync(
        "SELECT status, attempts, last_error, extract(epoch FROM not_before - now()) AS wait FROM outbox "
        "WHERE payload->>'text' = $1 ORDER BY id DESC LIMIT 1",
        text)[0];
  }
};

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
  isolate_outbox();
  PgOutbox outbox{db()};
  maxapi::RecordingBotApi api;
  maxapi::SendLimiter limiter{
      {.global_capacity = 100, .global_rate = 100, .chat_capacity = 100, .chat_rate = 100}};
  OutboxSender sender{db(), api, limiter, [] { return 1.0; }};
  const auto user = test::unique_id();
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "низкий"}, 0)));
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "высокий"}, 10)));
  EXPECT_EQ(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = ""}, 0)).error().code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(drogon::sync_wait(sender.drain(10)).value(), 2U);
  ASSERT_EQ(api.messages().size(), 2U);
  EXPECT_EQ(api.messages()[0].text, "высокий");  // приоритет раньше порядка

  // 5xx — повтор через 1 с × джиттер (здесь 1,0).
  api.fail_next_send();
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "повтор"}, 0)));
  EXPECT_EQ(drogon::sync_wait(sender.drain(10)).value(), 1U);
  const auto pending = outbox_row("повтор");
  EXPECT_EQ(pending["status"].as<std::string>(), "pending");
  EXPECT_EQ(pending["attempts"].as<int>(), 1);
  EXPECT_NEAR(pending["wait"].as<double>(), 1.0, 0.5);
  // Досрочно разрешаем повтор и отправляем.
  db()->execSqlSync("UPDATE outbox SET not_before = now() WHERE payload->>'text' = 'повтор'");
  EXPECT_EQ(drogon::sync_wait(sender.drain(10)).value(), 1U);
  EXPECT_EQ(api.messages().back().text, "повтор");

  // 429 — тоже повтор; прочие 4xx — сразу failed.
  api.fail_next_send(ErrorCode::kRateLimited);
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "429"}, 0)));
  EXPECT_EQ(drogon::sync_wait(sender.drain(1)).value(), 1U);
  EXPECT_EQ(outbox_row("429")["status"].as<std::string>(), "pending");
  std::this_thread::sleep_for(std::chrono::milliseconds{50});  // 429 обнулил глобальное ведро (1/r = 10 мс)
  api.fail_next_send(ErrorCode::kInvalidArgument);
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = user, .text = "400"}, 0)));
  EXPECT_EQ(drogon::sync_wait(sender.drain(1)).value(), 1U);
  EXPECT_EQ(outbox_row("400")["status"].as<std::string>(), "failed");

  // Испорченная строка — сразу failed.
  db()->execSqlSync(
      "INSERT INTO outbox (user_id, payload) SELECT id, '{\"version\":2}'::jsonb FROM app_user WHERE "
      "max_user_id = $1",
      user);
  EXPECT_GE(drogon::sync_wait(sender.drain(10)).value(), 1U);
  const auto failed =
      db()->execSqlSync("SELECT status FROM outbox WHERE payload->>'version' = '2' ORDER BY id DESC LIMIT 1");
  EXPECT_EQ(failed[0]["status"].as<std::string>(), "failed");
  EXPECT_TRUE(drogon::sync_wait(sender.recover_stale()).has_value());
}

TEST(OutboxRetryDelay, ExponentialCappedWithJitter) {
  using std::chrono::milliseconds;
  EXPECT_EQ(OutboxSender::retry_delay(1, 1.0), milliseconds{1000});
  EXPECT_EQ(OutboxSender::retry_delay(3, 1.0), milliseconds{4000});
  EXPECT_EQ(OutboxSender::retry_delay(3, 0.5), milliseconds{2000});
  EXPECT_EQ(OutboxSender::retry_delay(20, 1.0), milliseconds{300'000});  // потолок 5 минут
  EXPECT_EQ(OutboxSender::retry_delay(2, 7.0), milliseconds{2000});  // джиттер зажат в [0,5; 1]
  const auto jitter = OutboxSender::default_jitter();
  for (int i = 0; i < 100; ++i) {
    const auto j = jitter();
    EXPECT_GE(j, 0.5);
    EXPECT_LE(j, 1.0);
  }
}

// АРХ §7.6: сообщения одного чата не чаще лимита — лишние откладываются, другой чат не ждёт, попытки не
// тратятся.
TEST_F(PortsPgTest, OutboxSenderDefersChatOverLimit) {
  isolate_outbox();
  PgOutbox outbox{db()};
  maxapi::RecordingBotApi api;
  maxapi::SendLimiter limiter{};  // C = 1, r = 1/с на чат
  OutboxSender sender{db(), api, limiter, [] { return 1.0; }};
  const auto alice = test::unique_id();
  const auto bob = test::unique_id();
  for (const auto* t : {"a1", "a2", "a3"}) {
    ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = alice, .text = t}, 0)));
  }
  ASSERT_TRUE(drogon::sync_wait(outbox.enqueue({.max_user_id = bob, .text = "b1"}, 0)));
  ASSERT_TRUE(drogon::sync_wait(sender.drain(10)));
  ASSERT_EQ(api.messages().size(), 2U);  // a1 и b1
  EXPECT_EQ(api.messages()[0].text, "a1");
  EXPECT_EQ(api.messages()[1].text, "b1");
  for (const auto* t : {"a2", "a3"}) {
    const auto row = outbox_row(t);
    EXPECT_EQ(row["status"].as<std::string>(), "pending") << t;
    EXPECT_EQ(row["attempts"].as<int>(), 0) << t;   // откладывание — не попытка
    EXPECT_GT(row["wait"].as<double>(), 0.5) << t;  // ≈ 1 с до следующего токена чата
  }
  // Через секунду уходит ровно одно следующее сообщение чата — порядок сохранён.
  std::this_thread::sleep_for(std::chrono::milliseconds{1100});
  ASSERT_TRUE(drogon::sync_wait(sender.drain(10)));
  ASSERT_EQ(api.messages().size(), 3U);
  EXPECT_EQ(api.messages()[2].text, "a2");
}

TEST_F(PortsPgTest, DialogStoreWithTtl) {
  PgDialogStore store{db()};
  const auto user = test::unique_id();
  EXPECT_FALSE(drogon::sync_wait(store.get(user)).value().has_value());
  ASSERT_TRUE(drogon::sync_wait(store.set(user, {.state = "awaiting_inn", .arg = 42})));
  const auto d = drogon::sync_wait(store.get(user)).value();
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(test::checked(d).state, "awaiting_inn");
  EXPECT_EQ(test::checked(d).arg, 42);
  // Устаревший диалог не возвращается.
  db()->execSqlSync(
      "UPDATE dialog_state SET updated_at = now() - interval '31 minutes' WHERE user_id = (SELECT id FROM "
      "app_user "
      "WHERE max_user_id = $1)",
      user);
  EXPECT_FALSE(drogon::sync_wait(store.get(user)).value().has_value());
  ASSERT_TRUE(drogon::sync_wait(store.set(user, {.state = "awaiting_inn", .arg = 7})));
  const auto again = drogon::sync_wait(store.get(user)).value();
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(test::checked(again).arg, 7);
  ASSERT_TRUE(drogon::sync_wait(store.clear(user)));
  EXPECT_FALSE(drogon::sync_wait(store.get(user)).value().has_value());
}

TEST_F(PortsPgTest, SnapshotLoaderPicksLatestReadyPerRole) {
  SnapshotSet s;
  SnapshotLoader loader{db(), s};
  // Версии выше всех, что могут оказаться в общей БД, — чтобы стать «последними».
  const auto base = static_cast<std::uint64_t>(test::unique_id()) + 1'000'000'000'000ULL;
  const auto write = [&](std::uint64_t v, bool demo, const std::string& name) {
    const auto path = dir() / name;
    EXPECT_TRUE(snapshot::write_snapshot(
        path, {}, {.version = v, .source = "test", .source_date = kToday, .is_demo = demo}));
    return path.string();
  };
  const auto insert = [&](std::uint64_t v, bool demo, std::optional<int> stage, const std::string& path,
                          const std::string& status) {
    db()->execSqlSync(
        "INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo, demo_stage) "
        "VALUES ($1, 'test', '2026-09-26', $2, $3, $4, nullif($5::int, -1)::smallint)",
        static_cast<std::int64_t>(v), path, status, demo, stage.value_or(-1));
  };
  insert(base, false, std::nullopt, write(base, false, "prod.bin"), "ready");
  insert(base + 1, true, 0, write(base + 1, true, "demo0.bin"), "ready");
  insert(base + 2, true, 1, write(base + 2, true, "demo1.bin"), "ready");
  insert(base + 3, false, std::nullopt, write(base + 3, false, "prod-building.bin"), "building");  // не ready
  const auto first = drogon::sync_wait(loader.refresh());
  ASSERT_TRUE(first.has_value()) << first.error().detail;
  EXPECT_EQ(first.value().loaded.size(), 3U);
  EXPECT_EQ(s.get(SnapshotRole::kProd)->meta().version, base);
  EXPECT_EQ(s.get(SnapshotRole::kDemoBase)->meta().version, base + 1);
  EXPECT_EQ(s.get(SnapshotRole::kDemoUpdated)->meta().version, base + 2);
  EXPECT_EQ(s.for_user(true, DemoStage::kUpdated)->meta().version, base + 2);
  EXPECT_EQ(s.for_user(false, DemoStage::kUpdated)->meta().version, base);
  EXPECT_EQ(s.primary()->meta().version, base);
  EXPECT_TRUE(drogon::sync_wait(loader.refresh()).value().loaded.empty());  // без изменений
  // Новая боевая версия с битым путём: ошибка только у этой роли, старый снапшот остаётся (АРХ §4).
  insert(base + 4, false, std::nullopt, "/nonexistent", "ready");
  const auto broken = drogon::sync_wait(loader.refresh());
  ASSERT_TRUE(broken.has_value());
  EXPECT_EQ(broken.value().errors.size(), 1U);
  EXPECT_EQ(s.get(SnapshotRole::kProd)->meta().version, base);
  db()->execSqlSync("DELETE FROM snapshot_version WHERE version BETWEEN $1 AND $2",
                    static_cast<std::int64_t>(base), static_cast<std::int64_t>(base + 4));
}

TEST_F(PortsPgTest, JobQueueLeaseRetryAndExhaustion) {
  JobQueue q{db()};
  const auto kind = "test-" + std::to_string(test::unique_id());
  EXPECT_TRUE(drogon::sync_wait(q.enqueue(kind, "k1", "{\"n\":1}", std::chrono::seconds{0})).value());
  EXPECT_FALSE(
      drogon::sync_wait(q.enqueue(kind, "k1", "{\"n\":1}", std::chrono::seconds{0})).value());  // дубль
  JobRunner runner{q};
  Probe probe;
  // Корутина — свободная функция (CLAUDE.md, «Корутины»); лямбда без co_await лишь передаёт указатель.
  runner.on(kind, [&probe](std::string payload) { return probe_handler(&probe, std::move(payload)); });
  // Прочие задачи общей БД не трогаем: отдельный исполнитель видит только свою задачу через run_at.
  db()->execSqlSync("UPDATE job SET run_at = now() + interval '1 hour' WHERE kind <> $1 AND run_at <= now()",
                    kind);
  EXPECT_EQ(drogon::sync_wait(runner.run_ready(5)).value(), 0U);
  EXPECT_EQ(probe.calls, 1);
  const auto row = db()->execSqlSync(
      "SELECT attempts, last_error, locked_until IS NULL AS free, extract(epoch FROM run_at - now()) AS wait "
      "FROM job "
      "WHERE kind = $1",
      kind)[0];
  EXPECT_EQ(row["attempts"].as<int>(), 1);
  EXPECT_EQ(row["last_error"].as<std::string>(), "сбой");
  EXPECT_TRUE(row["free"].as<bool>());
  EXPECT_NEAR(row["wait"].as<double>(), 30.0, 5.0);  // backoff(1) = 30 с
  // Исчерпание попыток — задача замирает с причиной.
  db()->execSqlSync("UPDATE job SET run_at = now(), attempts = $2 WHERE kind = $1", kind,
                    JobQueue::kMaxAttempts - 1);
  EXPECT_EQ(drogon::sync_wait(runner.run_ready(5)).value(), 0U);
  EXPECT_EQ(db()->execSqlSync("SELECT run_at = 'infinity' AS dead FROM job WHERE kind = $1", kind)[0]["dead"]
                .as<bool>(),
            true);
  // Успех — задача удаляется.
  probe.ok = true;
  db()->execSqlSync("UPDATE job SET run_at = now(), attempts = 0 WHERE kind = $1", kind);
  EXPECT_EQ(drogon::sync_wait(runner.run_ready(5)).value(), 1U);
  EXPECT_EQ(db()->execSqlSync("SELECT count(*) AS n FROM job WHERE kind = $1", kind)[0]["n"].as<int>(), 0);
  // Неизвестный вид задачи — ошибка, задача остаётся для разбора.
  const auto orphan = "orphan-" + std::to_string(test::unique_id());
  ASSERT_TRUE(drogon::sync_wait(q.enqueue(orphan, "x", "{}", std::chrono::seconds{0})));
  EXPECT_EQ(drogon::sync_wait(runner.run_ready(5)).value(), 0U);
  EXPECT_EQ(db()->execSqlSync("SELECT count(*) AS n FROM job WHERE kind = $1", orphan)[0]["n"].as<int>(), 1);
  db()->execSqlSync("DELETE FROM job WHERE kind = $1", orphan);
  EXPECT_EQ(JobQueue::backoff(1), std::chrono::seconds{30});
  EXPECT_EQ(JobQueue::backoff(3), std::chrono::seconds{120});
  EXPECT_EQ(JobQueue::backoff(20), std::chrono::seconds{3600});
}

// F5: смена статуса наблюдаемого документа → ровно одно уведомление, даже если процесс упал посреди задачи.
TEST_F(PortsPgTest, NotifyVersionIdempotentAcrossCrash) {
  // Боевые пользователи в общей БД — только артефакты этого теста.
  db()->execSqlSync("DELETE FROM app_user WHERE NOT is_demo");
  const auto v = static_cast<std::uint64_t>(test::unique_id()) + 2'000'000'000'000ULL;
  db()->execSqlSync(
      "INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo) "
      "VALUES ($1, 'test', '2026-09-27', 'x', 'building', false)",
      static_cast<std::int64_t>(v));
  const std::vector<snapshot::RecordInput> records{
      {.number = "RUD-CR.PA08.B.89369/26", .status = snapshot::Status::kSuspended},
      {.number = "RUD-CN.PA01.B.10002/25", .status = snapshot::Status::kTerminated},
      {.number = "RUD-RU.PA01.B.10001/25", .status = snapshot::Status::kActive},
  };
  ASSERT_TRUE(snapshot::write_snapshot(dir() / "prod.bin", records,
                                       {.version = v, .source = "fsa", .source_date = kToday}));
  const auto snap = snapshot::open_snapshot(dir() / "prod.bin").value();
  const auto user = test::unique_id();
  db()->execSqlSync("INSERT INTO app_user (max_user_id, is_demo) VALUES ($1, false)", user);
  // Знал: всё действует, версия раньше v. Изменились два документа из трёх; четвёртого нет в данных.
  for (const auto* key : {"RUD-CR.PA08.B.89369/26", "RUD-CN.PA01.B.10002/25", "RUD-RU.PA01.B.10001/25",
                          "RUD-XX.PA01.B.99999/25"}) {
    db()->execSqlSync(
        "INSERT INTO portfolio_item (user_id, doc_key, doc_kind, last_status, last_version) "
        "SELECT id, $2, 'declaration', 'active', 1 FROM app_user WHERE max_user_id = $1",
        user, std::string{key});
  }
  NotifyService notify{db(), renderer(), 1};  // пачка по одной строке — «падение» между пачками
  const auto partial = drogon::sync_wait(notify.notify_version(snap, 2));  // две пачки и «падение»
  ASSERT_TRUE(partial.has_value()) << partial.error().detail;
  EXPECT_EQ(partial.value().checked, 2U);
  const auto rest = drogon::sync_wait(notify.notify_version(snap));  // перезапуск задачи
  ASSERT_TRUE(rest.has_value());
  EXPECT_EQ(rest.value().checked, 2U);
  EXPECT_EQ(partial.value().notified + rest.value().notified, 2U);
  const auto again = drogon::sync_wait(notify.notify_version(snap));  // второй плановый прогон
  EXPECT_EQ(again.value().checked, 0U);
  const auto counts = db()->execSqlSync(
      "SELECT (SELECT count(*) FROM notification n JOIN portfolio_item p ON p.id = n.portfolio_item_id "
      "        JOIN app_user u ON u.id = p.user_id WHERE u.max_user_id = $1) AS notifications, "
      "(SELECT count(*) FROM outbox o JOIN app_user u ON u.id = o.user_id WHERE u.max_user_id = $1) AS "
      "messages, "
      "(SELECT count(*) FROM portfolio_item p JOIN app_user u ON u.id = p.user_id "
      "        WHERE u.max_user_id = $1 AND p.last_version = $2) AS at_v",
      user, static_cast<std::int64_t>(v))[0];
  EXPECT_EQ(counts["notifications"].as<int>(), 2);
  EXPECT_EQ(counts["messages"].as<int>(), 2);  // по сообщению на пачку с изменением
  EXPECT_EQ(counts["at_v"].as<int>(), 4);  // сверены все, включая отсутствующий в данных
  db()->execSqlSync("DELETE FROM app_user WHERE max_user_id = $1", user);
  db()->execSqlSync("DELETE FROM snapshot_version WHERE version = $1", static_cast<std::int64_t>(v));
}

TEST_F(PortsPgTest, CleanupRetention) {
  const auto user = test::unique_id();
  db()->execSqlSync("INSERT INTO app_user (max_user_id) VALUES ($1)", user);
  const auto key = "m:old-" + std::to_string(user);
  db()->execSqlSync(
      "INSERT INTO inbound_update (dedup_key, received_at) VALUES ($1, now() - interval '8 days')", key);
  db()->execSqlSync(
      "INSERT INTO check_log (user_id, via, level, latency_ms, created_at) SELECT id, 'bot_text', 'ok', 1, "
      "now() - "
      "interval '91 days' FROM app_user WHERE max_user_id = $1",
      user);
  db()->execSqlSync(
      "INSERT INTO outbox (user_id, payload, status, sent_at) SELECT id, '{}'::jsonb, 'sent', now() - "
      "interval "
      "'31 days' FROM app_user WHERE max_user_id = $1",
      user);
  db()->execSqlSync(
      "INSERT INTO outbox (user_id, payload, status) SELECT id, '{\"keep\":1}'::jsonb, 'pending' FROM "
      "app_user "
      "WHERE max_user_id = $1",
      user);
  const auto r = drogon::sync_wait(cleanup_retention(db()));
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  EXPECT_GE(r.value().inbound_update, 1U);
  EXPECT_GE(r.value().check_log, 1U);
  EXPECT_GE(r.value().outbox, 1U);
  const auto left = db()->execSqlSync(
      "SELECT count(*) AS n FROM outbox o JOIN app_user u ON u.id = o.user_id WHERE u.max_user_id = $1",
      user);
  EXPECT_EQ(left[0]["n"].as<int>(), 1);  // ожидающее сообщение не удаляется
  db()->execSqlSync("DELETE FROM app_user WHERE max_user_id = $1", user);
  EXPECT_EQ(notify_dedup_key(7, 2), "7:2");
}

}  // namespace
}  // namespace sk::certd
