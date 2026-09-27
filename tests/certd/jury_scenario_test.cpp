/// Сценарий жюри целиком (АРХ §2, F5 + F6) на настоящем PostgreSQL: события webhook MAX → бот → домен →
/// «Симулировать обновление» → notify_changes → outbox → отправитель с лимитером → Bot API.
/// Данные — настоящие демо-источники data/demo (N и N+1), файл — PDF-выписка из фикстур.
#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>

#include "bot/bot.hpp"
#include "bot/card.hpp"
#include "build.hpp"
#include "demo_source.hpp"
#include "domain_service.hpp"
#include "notify.hpp"
#include "pg_ports.hpp"
#include "sertkontrol/maxapi/fake_bot_api.hpp"
#include "support/files.hpp"
#include "support/pg.hpp"
#include "webhook.hpp"

namespace sk::certd {
namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr std::string_view kSecret = "jury-secret-1";
constexpr std::string_view kFileUrl = "https://files.example/extract-89369-26";

std::string compact(const Json::Value& v) {
  Json::StreamWriterBuilder b;
  b["indentation"] = "";
  return Json::writeString(b, v);
}

Json::Value parse(const std::string& s) {
  Json::Value v;
  std::string errors;
  const std::unique_ptr<Json::CharReader> r{Json::CharReaderBuilder{}.newCharReader()};
  r->parse(s.data(), s.data() + s.size(), &v, &errors);
  return v;
}

class JuryScenarioTest : public ::testing::Test {
 public:
  void SetUp() override {
    const auto pg = test::test_pg();
    if (!pg) {
      GTEST_SKIP() << "SK_TEST_PG не задан: запускайте через scripts/ci/with-pg.sh";
    }
    db_ = drogon::orm::DbClient::newPgClient(*pg, 2);
    dir_ = std::filesystem::temp_directory_path() / ("sk-jury-" + std::to_string(test::unique_id()));
    std::filesystem::create_directories(dir_);
    // Демо-пара из настоящих источников; версии уникальны, чтобы не пересекаться с другими тестами общей БД.
    version_ = static_cast<std::uint64_t>(test::unique_id()) + 3'000'000'000'000ULL;
    for (const auto& [file, v, stage, role] :
         {std::tuple{"base.tsv", version_, 0, SnapshotRole::kDemoBase},
          std::tuple{"next.tsv", version_ + 1, 1, SnapshotRole::kDemoUpdated}}) {
      auto src = ingest::DemoTsvSource::open(std::filesystem::path{SK_DEMO_DIR} / file);
      ASSERT_TRUE(src.has_value()) << src.error().detail;
      const auto built = ingest::build_snapshot(src.value(), dir_, v, true);
      ASSERT_TRUE(built.has_value()) << built.error().detail;
      snapshots_.set(role, snapshot::open_snapshot(built.value().file).value());
      db_->execSqlSync(
          "INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo, "
          "demo_stage) "
          "VALUES ($1, 'demo', '2026-09-26', $2, 'building', true, $3::smallint)",
          static_cast<std::int64_t>(v), built.value().file.string(), static_cast<std::int16_t>(stage));
    }
    // Очередь отправителя — только сообщения теста.
    db_->execSqlSync("UPDATE outbox SET status = 'failed' WHERE status IN ('pending', 'sending')");
    notify_ = std::make_unique<NotifyService>(
        db_, [](const ChangeNotice& n) { return bot::render_change_notice(n, {}); });
    domain_ = std::make_unique<DomainServiceImpl>(db_, snapshots_, pool_, limiter_, *notify_,
                                                  [] { return year{2026} / month{9} / day{27}; });
    outbox_ = std::make_unique<PgOutbox>(db_);
    inbound_ = std::make_unique<PgInboundLog>(db_);
    dialogs_ = std::make_unique<PgDialogStore>(db_);
    bot_ = std::make_unique<bot::Bot>(*domain_, *outbox_, *dialogs_, api_, bot::BotConfig{});
    webhook_ = std::make_unique<Webhook>(*bot_, *inbound_, std::string{kSecret});
    sender_ = std::make_unique<OutboxSender>(db_, api_, send_limiter_, [] { return 1.0; });
    api_.add_file(std::string{kFileUrl}, [] {
      const auto s = test::read_file(std::filesystem::path{SK_FIXTURES_DIR} / "pdf" / "extract-89369-26.pdf");
      std::vector<std::byte> out(s.size());
      std::ranges::transform(s, out.begin(), [](char c) { return static_cast<std::byte>(c); });
      return out;
    }());
  }

  void TearDown() override {
    if (!db_) {
      return;
    }
    db_->execSqlSync("DELETE FROM app_user WHERE max_user_id = $1", user_);
    db_->execSqlSync("DELETE FROM snapshot_version WHERE version IN ($1, $2)",
                     static_cast<std::int64_t>(version_), static_cast<std::int64_t>(version_ + 1));
    std::filesystem::remove_all(dir_);
  }

  /// POST /max/webhook и ожидание фоновой обработки события (webhook отвечает 200 до неё).
  void deliver(const Json::Value& update, const std::string& dedup_key) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->addHeader("X-Max-Bot-Api-Secret", std::string{kSecret});
    req->setBody(compact(update));
    ASSERT_EQ(drogon::sync_wait(webhook_->handle(req))->getStatusCode(), drogon::k200OK);
    for (int i = 0; i < 100; ++i) {
      const auto r = db_->execSqlSync(
          "SELECT processed_at IS NOT NULL AS done, coalesce(error, '') AS error FROM inbound_update "
          "WHERE dedup_key = $1",
          dedup_key);
      if (!r.empty() && r[0]["done"].as<bool>()) {
        EXPECT_EQ(r[0]["error"].as<std::string>(), "") << dedup_key;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    FAIL() << "событие " << dedup_key << " не обработано за 5 с";
  }

  [[nodiscard]] Json::Value user_json() const {
    Json::Value u;
    u["user_id"] = static_cast<Json::Int64>(user_);
    u["first_name"] = "Жюри";
    u["is_bot"] = false;
    return u;
  }

  void callback(const std::string& payload, const std::string& id) {
    Json::Value cb;
    cb["timestamp"] = static_cast<Json::Int64>(1790000001000);
    cb["callback_id"] = id;
    cb["payload"] = payload;
    cb["user"] = user_json();
    Json::Value u;
    u["update_type"] = "message_callback";
    u["timestamp"] = static_cast<Json::Int64>(1790000001000);
    u["callback"] = cb;
    deliver(u, "c:" + id);
  }

  /// Сообщения outbox пользователя (C9) в порядке постановки.
  [[nodiscard]] std::vector<Json::Value> outbox_messages() const {
    std::vector<Json::Value> out;
    for (const auto& row :
         db_->execSqlSync("SELECT o.payload::text AS p FROM outbox o JOIN app_user a ON a.id = o.user_id "
                          "WHERE a.max_user_id = $1 ORDER BY o.id",
                          user_)) {
      out.push_back(parse(row["p"].as<std::string>()));
    }
    return out;
  }

  drogon::orm::DbClientPtr db_;
  std::filesystem::path dir_;
  std::uint64_t version_{0};
  std::int64_t user_{test::unique_id()};
  SnapshotSet snapshots_;
  RateLimiter limiter_{30};
  RecognitionPool pool_{1, 4, recog::recognize};
  maxapi::RecordingBotApi api_;
  // Лимиты MAX проверяются в maxapi_test и OutboxSenderDefersChatOverLimit; здесь чат быстрее, чтобы тест не
  // ждал секундами.
  maxapi::SendLimiter send_limiter_{{.chat_capacity = 1, .chat_rate = 20}};
  std::unique_ptr<NotifyService> notify_;
  std::unique_ptr<DomainServiceImpl> domain_;
  std::unique_ptr<PgOutbox> outbox_;
  std::unique_ptr<PgInboundLog> inbound_;
  std::unique_ptr<PgDialogStore> dialogs_;
  std::unique_ptr<bot::Bot> bot_;
  std::unique_ptr<Webhook> webhook_;
  std::unique_ptr<OutboxSender> sender_;
};

TEST_F(JuryScenarioTest, ExtractWatchSimulateNotify) {
  // 1. Старт и согласие.
  Json::Value started;
  started["update_type"] = "bot_started";
  started["timestamp"] = static_cast<Json::Int64>(1790000000000);
  started["chat_id"] = 1;
  started["user"] = user_json();
  deliver(started, "s:" + std::to_string(user_) + ":1790000000000");
  callback("c:1", "jury-consent-" + std::to_string(user_));

  // 2. Выписка PDF → «Проверяю…» и карточка «действует».
  Json::Value attachment;
  attachment["type"] = "file";
  attachment["payload"]["url"] = std::string{kFileUrl};
  attachment["payload"]["token"] = "tok";
  attachment["filename"] = "extract.pdf";
  attachment["size"] = 40000;
  Json::Value message;
  message["sender"] = user_json();
  message["body"]["mid"] = "jury-mid-" + std::to_string(user_);
  message["body"]["attachments"].append(attachment);
  Json::Value created;
  created["update_type"] = "message_created";
  created["timestamp"] = static_cast<Json::Int64>(1790000002000);
  created["message"] = message;
  deliver(created, "m:jury-mid-" + std::to_string(user_));
  auto msgs = outbox_messages();
  ASSERT_GE(msgs.size(), 4U);  // приветствие, справка, «Проверяю…», карточка
  const auto card = msgs.back();
  EXPECT_EQ(card["kind"].asString(), "verdict");
  EXPECT_NE(card["text"].asString().find("89369/26</b> — действует"), std::string::npos)
      << card["text"].asString();
  EXPECT_NE(card["text"].asString().find("Тестовые данные"), std::string::npos);
  const auto watch = card["buttons"][0][0]["payload"].asString();
  ASSERT_TRUE(watch.starts_with("w:"));

  // 3. «На контроль».
  callback(watch, "jury-watch-" + std::to_string(user_));
  EXPECT_NE(outbox_messages().back()["text"].asString().find("на контроле"), std::string::npos);

  // Повтор того же события MAX не обрабатывается второй раз.
  const auto before_replay = outbox_messages().size();
  deliver(created, "m:jury-mid-" + std::to_string(user_));
  EXPECT_EQ(outbox_messages().size(), before_replay);

  // 4. «Симулировать обновление» (кнопка мини-приложения → тот же DomainService).
  const auto sim =
      drogon::sync_wait(domain_->simulate_update({.max_user_id = user_, .channel = Channel::kApp}));
  ASSERT_TRUE(sim.has_value()) << sim.error().detail;
  EXPECT_EQ(sim.value().notified, 1U);

  // 5. Отправитель доставляет всё, включая уведомление, — ровно одно.
  for (int i = 0; i < 20 && drogon::sync_wait(sender_->drain(50)).value() > 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{60});  // следующий токен чата
  }
  std::size_t notices = 0;
  for (const auto& m : api_.messages()) {
    if (m.kind == maxapi::MessageKind::kStatusChanged) {
      ++notices;
      EXPECT_NE(m.text.find("<b>приостановлен</b> до 26.12.2026"), std::string::npos) << m.text;
      EXPECT_NE(m.text.find("(было: действует)"), std::string::npos);
      EXPECT_NE(m.text.find("Данные реестра на 26.09.2026"), std::string::npos);
    }
  }
  EXPECT_EQ(notices, 1U);
  EXPECT_EQ(api_.messages().size(), outbox_messages().size());  // всё поставленное доставлено

  // Повтор «Симулировать обновление» — без второго уведомления (F5: ровно одно сообщение).
  EXPECT_EQ(drogon::sync_wait(domain_->simulate_update({.max_user_id = user_})).value().notified, 0U);
}

}  // namespace
}  // namespace sk::certd
