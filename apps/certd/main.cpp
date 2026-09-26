/// @file main.cpp
/// @brief Точка входа `certd`: сборка компонентов и маршрутов. Логика — в модулях, здесь только связи.
#include <drogon/drogon.h>

#include <atomic>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

#include "bot/bot.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "domain_service.hpp"
#include "health.hpp"
#include "pg_ports.hpp"
#include "rest_api.hpp"
#include "snapshot_loader.hpp"
#include "webhook.hpp"

namespace {

using sk::certd::Config;

/// Интервал проверки новой версии снапшота. Этап 2 заменит опрос на `LISTEN snapshot_ready`.
constexpr double kSnapshotPollSeconds = 5.0;
constexpr double kOutboxPollSeconds = 0.3;
constexpr std::size_t kOutboxBatch = 20;
/// Лимит тела запроса: вложение 20 МБ + multipart-обвязка (АРХ §10).
constexpr std::size_t kMaxBodyBytes = std::size_t{21} * 1024 * 1024;

/// Обработчик `/healthz`: БД доступна и снапшот загружен (АРХ §3).
class HealthEndpoint {
 public:
  HealthEndpoint(drogon::orm::DbClientPtr db, const sk::snapshot::SnapshotHolder& holder)
      : db_(std::move(db)), holder_(holder) {}

  drogon::Task<drogon::HttpResponsePtr> handle(drogon::HttpRequestPtr /*req*/) const {
    bool db_ok = true;
    try {
      co_await db_->execSqlCoro("SELECT 1");
    } catch (const drogon::orm::DrogonDbException& e) {
      LOG_WARN << "healthz: БД недоступна: " << e.base().what();
      db_ok = false;
    }
    std::optional<std::uint64_t> version;
    if (const auto snap = holder_.get()) {
      version = snap->meta().version;
    }
    const auto report =
        sk::certd::evaluate_health({.db_ok = db_ok, .snapshot_version = version, .snapshot_required = true});
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(report.http_status));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(report.body);
    co_return resp;
  }

 private:
  drogon::orm::DbClientPtr db_;
  const sk::snapshot::SnapshotHolder& holder_;
};

drogon::Task<void> refresh_snapshot(sk::certd::SnapshotLoader* loader) {
  const auto r = co_await loader->refresh();
  if (!r) {
    LOG_WARN << "снапшот: " << r.error().detail;
  }
}

drogon::Task<void> drain_outbox(sk::certd::OutboxSender* sender, std::atomic<bool>* busy) {
  const auto r = co_await sender->drain(kOutboxBatch);
  if (!r) {
    LOG_WARN << "outbox: " << r.error().detail;
  }
  *busy = false;
}

drogon::Task<void> recover_outbox(sk::certd::OutboxSender* sender) {
  const auto r = co_await sender->recover_stale();
  if (r && r.value() > 0) {
    LOG_INFO << "outbox: возвращено в очередь " << r.value();
  }
}

int run() {
  auto cfg_result = sk::certd::load_config(sk::certd::process_env);
  if (!cfg_result) {
    std::cerr << "certd: ошибка конфигурации: " << cfg_result.error().detail << '\n';
    return 2;
  }
  const Config cfg = std::move(cfg_result).value();
  auto& app = drogon::app();

  auto db = drogon::orm::DbClient::newPgClient(cfg.pg_conninfo, cfg.db_connections);
  sk::snapshot::SnapshotHolder holder;
  sk::certd::SnapshotLoader loader{db, holder};
  sk::certd::RateLimiter limiter;
  sk::certd::RecognitionPool recognition{cfg.recog_threads, cfg.recog_queue, sk::recog::recognize};
  sk::certd::DomainServiceImpl domain{db, holder, recognition, limiter, [] {
                                        return sk::certd::moscow_date(std::chrono::system_clock::now());
                                      }};

  const auto health = std::make_shared<HealthEndpoint>(db, holder);
  app.registerHandler("/healthz",
                      [health](drogon::HttpRequestPtr req) { return health->handle(std::move(req)); },
                      {drogon::Get});
  sk::certd::RestApi::register_routes(
      app,
      std::make_shared<sk::certd::RestApi>(
          domain, sk::certd::AuthConfig{.bot_token = cfg.max_bot_token, .dev_user_id = cfg.dev_user_id}));
  if (cfg.dev_user_id) {
    LOG_WARN << "CERTD_DEV_USER_ID: мини-приложение без MAX работает от пользователя " << *cfg.dev_user_id
             << " (ADR-0013, только локально)";
  }

  // Бот и отправитель outbox — только при заданном токене.
  std::optional<sk::maxapi::HttpBotApi> bot_api;
  std::optional<sk::certd::PgOutbox> outbox;
  std::optional<sk::certd::PgInboundLog> inbound;
  std::optional<sk::certd::bot::Bot> bot;
  std::optional<sk::certd::Webhook> webhook;
  std::optional<sk::certd::OutboxSender> sender;
  std::atomic<bool> sending{false};
  if (cfg.bot_enabled()) {
    bot_api.emplace(sk::maxapi::BotApiConfig{
        .base_url = cfg.max_api_base_url, .token = cfg.max_bot_token, .bot_username = cfg.max_bot_username});
    outbox.emplace(db);
    inbound.emplace(db);
    bot.emplace(domain, *outbox, *bot_api,
                sk::certd::bot::BotConfig{.card = {.open_app = !cfg.max_bot_username.empty()}});
    webhook.emplace(*bot, *inbound, cfg.max_webhook_secret);
    sender.emplace(db, *bot_api);
    auto* hook = &*webhook;
    app.registerHandler("/max/webhook",
                        [hook](drogon::HttpRequestPtr req) { return hook->handle(std::move(req)); },
                        {drogon::Post});
  } else {
    LOG_WARN << "MAX_BOT_TOKEN не задан: бот выключен, работают /healthz, REST и мини-приложение";
  }

  app.getLoop()->runAfter(0.0, [&loader, &sender] {
    drogon::async_run([&loader] { return refresh_snapshot(&loader); });
    if (sender) {
      drogon::async_run([&sender] { return recover_outbox(&*sender); });
    }
  });
  app.getLoop()->runEvery(kSnapshotPollSeconds,
                          [&loader] { drogon::async_run([&loader] { return refresh_snapshot(&loader); }); });
  if (sender) {
    app.getLoop()->runEvery(kOutboxPollSeconds, [&sender, &sending] {
      if (!sending.exchange(true)) {
        drogon::async_run([&sender, &sending] { return drain_outbox(&*sender, &sending); });
      }
    });
  }

  LOG_INFO << "certd: порт " << cfg.port << ", статика " << cfg.web_root.string();
  // Файлы пользователей на диск не сохраняются (АРХ §6). Временный каталог Drogon — в /tmp,
  // иначе он создаётся в рабочем каталоге, куда у непривилегированного пользователя нет прав.
  app.setDocumentRoot(cfg.web_root.string())
      .setUploadPath((std::filesystem::temp_directory_path() / "certd-uploads").string())
      .setClientMaxBodySize(kMaxBodyBytes)
      .setClientMaxMemoryBodySize(kMaxBodyBytes)
      .setThreadNum(cfg.threads)
      .addListener("0.0.0.0", cfg.port)
      .run();
  return 0;
}

}  // namespace

int main() {
  try {
    return run();
  } catch (const std::exception& e) {
    std::cerr << "certd: неперехваченное исключение: " << e.what() << '\n';
  } catch (...) {
    std::cerr << "certd: неперехваченное исключение неизвестного типа\n";
  }
  return 1;
}
