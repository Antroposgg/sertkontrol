/// @file main.cpp
/// @brief Точка входа `certd`: сборка компонентов, маршрутов и фоновых циклов. Логика — в модулях.
#include <drogon/drogon.h>
#include <drogon/orm/DbListener.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

#include "bot/bot.hpp"
#include "bot/card.hpp"
#include "bot_identity.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "domain_service.hpp"
#include "health.hpp"
#include "jobs.hpp"
#include "json_views.hpp"
#include "maintenance.hpp"
#include "notify.hpp"
#include "pg_ports.hpp"
#include "rest_api.hpp"
#include "snapshot_loader.hpp"
#include "webhook.hpp"

namespace {

using sk::certd::Config;

/// Страховочный опрос `snapshot_version`: NOTIFY теряется, пока `certd` лежит или слушатель переподключается.
constexpr double kSnapshotPollSeconds = 60.0;
constexpr double kOutboxPollSeconds = 0.3;
constexpr std::size_t kOutboxBatch = 20;
constexpr double kJobPollSeconds = 1.0;
constexpr std::size_t kJobBatch = 10;
/// Лимит тела запроса: вложение 20 МБ + multipart-обвязка (АРХ §10).
constexpr std::size_t kMaxBodyBytes = std::size_t{21} * 1024 * 1024;

/// Обработчик `/healthz`: БД доступна и снапшот загружен (АРХ §3).
class HealthEndpoint {
 public:
  HealthEndpoint(drogon::orm::DbClientPtr db, const sk::certd::SnapshotSet& snapshots)
      : db_(std::move(db)), snapshots_(snapshots) {}

  drogon::Task<drogon::HttpResponsePtr> handle(drogon::HttpRequestPtr /*req*/) const {
    bool db_ok = true;
    try {
      co_await db_->execSqlCoro("SELECT 1");
    } catch (const drogon::orm::DrogonDbException& e) {
      LOG_WARN << "healthz: БД недоступна: " << e.base().what();
      db_ok = false;
    }
    std::optional<std::uint64_t> version;
    if (const auto snap = snapshots_.primary()) {
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
  const sk::certd::SnapshotSet& snapshots_;
};

/// Фоновые циклы: загрузка снапшотов, задачи, отправка. Флаги не дают одному циклу работать в две корутины.
struct Background {
  sk::certd::SnapshotLoader* loader{nullptr};
  sk::certd::JobQueue* jobs{nullptr};
  sk::certd::JobRunner* runner{nullptr};
  sk::certd::OutboxSender* sender{nullptr};
  std::atomic<bool> refreshing{false};
  std::atomic<bool> refresh_again{false};
  std::atomic<bool> running_jobs{false};
  std::atomic<bool> sending{false};
};

drogon::Task<void> refresh_snapshots(Background* bg);

void trigger_refresh(Background* bg) {
  if (bg->refreshing.exchange(true)) {
    bg->refresh_again = true;  // придёт ещё раз после текущего прогона
    return;
  }
  drogon::async_run([bg] { return refresh_snapshots(bg); });
}

drogon::Task<void> refresh_snapshots(Background* bg) {
  bool again = true;
  while (again) {
    bg->refresh_again = false;
    const auto r = co_await bg->loader->refresh();
    again = bg->refresh_again.exchange(false);
    if (!r) {
      LOG_WARN << "снапшот: " << r.error().detail;
      continue;
    }
    for (const auto& loaded : r.value().loaded) {
      if (loaded.role != sk::certd::SnapshotRole::kProd) {
        continue;  // демо-уведомления — только по «Симулировать обновление» конкретного пользователя (F6)
      }
      // Новая боевая версия → уведомления сейчас и повторно через 10 минут (ТЗ R3.9).
      const auto payload = "{\"version\":" + std::to_string(loaded.version) + "}";
      (void)co_await bg->jobs->enqueue(std::string{sk::certd::kJobNotifyChanges},
                                       sk::certd::notify_dedup_key(loaded.version, 1), payload,
                                       std::chrono::seconds{0});
      (void)co_await bg->jobs->enqueue(std::string{sk::certd::kJobNotifyChanges},
                                       sk::certd::notify_dedup_key(loaded.version, 2), payload,
                                       sk::certd::kNotifySecondPass);
    }
    again = again || bg->refresh_again.exchange(false);
  }
  bg->refreshing = false;
}

drogon::Task<void> run_jobs(Background* bg) {
  const auto r = co_await bg->runner->run_ready(kJobBatch);
  if (!r) {
    LOG_WARN << "задачи: " << r.error().detail;
  }
  bg->running_jobs = false;
}

drogon::Task<void> drain_outbox(Background* bg) {
  const auto r = co_await bg->sender->drain(kOutboxBatch);
  if (!r) {
    LOG_WARN << "outbox: " << r.error().detail;
  }
  bg->sending = false;
}

drogon::Task<void> recover_outbox(sk::certd::OutboxSender* sender) {
  const auto r = co_await sender->recover_stale();
  if (r && r.value() > 0) {
    LOG_INFO << "outbox: возвращено в очередь " << r.value();
  }
}

/// Сегодняшняя дата по Москве — ключ дедупликации ежедневной чистки.
std::string moscow_today_key() {
  return sk::certd::iso_date(sk::certd::moscow_date(std::chrono::system_clock::now()));
}

drogon::Task<void> schedule_cleanup(sk::certd::JobQueue* jobs) {
  (void)co_await jobs->enqueue(std::string{sk::certd::kJobCleanup}, moscow_today_key(), "{}",
                               std::chrono::seconds{0});
}

/// Обработчики задач. Методы, а не лямбды-корутины: лямбды ниже только возвращают `Task`.
class JobHandlers {
 public:
  JobHandlers(drogon::orm::DbClientPtr db, const sk::certd::SnapshotSet& snapshots,
              sk::certd::NotifyService& notify, sk::certd::JobQueue& jobs)
      : db_(std::move(db)), snapshots_(snapshots), notify_(notify), jobs_(jobs) {}

  drogon::Task<sk::Result<sk::Ok>> notify_changes(std::string payload) {
    Json::Value v;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader{Json::CharReaderBuilder{}.newCharReader()};
    if (!reader->parse(payload.data(), payload.data() + payload.size(), &v, &errors) ||
        !v["version"].isUInt64()) {
      co_return sk::Error{sk::ErrorCode::kInvalidArgument, "notify_changes: нет version в " + payload};
    }
    const auto version = v["version"].asUInt64();
    auto snap = snapshots_.get(sk::certd::SnapshotRole::kProd);
    // Загружена более новая версия — сверяемся с ней: сравнение с портфелем покрывает и пропущенные версии.
    if (!snap || snap->meta().version < version) {
      co_return sk::Error{sk::ErrorCode::kSnapshotUnavailable,
                          "боевая версия " + std::to_string(version) + " ещё не загружена"};
    }
    const auto stats = co_await notify_.notify_version(std::move(snap));
    if (!stats) {
      co_return stats.error();
    }
    LOG_INFO << "notify_changes v" << version << ": сверено " << stats.value().checked << ", изменилось "
             << stats.value().notified << ", сообщений " << stats.value().messages;
    co_return sk::Ok{};
  }

  drogon::Task<sk::Result<sk::Ok>> cleanup(std::string /*payload*/) {
    const auto r = co_await sk::certd::cleanup_retention(db_);
    if (!r) {
      co_return r.error();
    }
    LOG_INFO << "чистка: check_log " << r.value().check_log << ", inbound_update " << r.value().inbound_update
             << ", outbox " << r.value().outbox << ", dialog_state " << r.value().dialog_state;
    // Завтрашняя чистка — через сутки; ключ дня не даёт задвоить её после рестарта.
    const auto tomorrow =
        sk::certd::iso_date(sk::certd::moscow_date(std::chrono::system_clock::now()) + std::chrono::days{1});
    (void)co_await jobs_.enqueue(std::string{sk::certd::kJobCleanup}, tomorrow, "{}", std::chrono::hours{24});
    co_return sk::Ok{};
  }

 private:
  drogon::orm::DbClientPtr db_;
  const sk::certd::SnapshotSet& snapshots_;
  sk::certd::NotifyService& notify_;
  sk::certd::JobQueue& jobs_;
};

int run() {
  auto cfg_result = sk::certd::load_config(sk::certd::process_env);
  if (!cfg_result) {
    std::cerr << "certd: ошибка конфигурации: " << cfg_result.error().detail << '\n';
    return 2;
  }
  const Config cfg = std::move(cfg_result).value();
  auto& app = drogon::app();

  auto db = drogon::orm::DbClient::newPgClient(cfg.pg_conninfo, cfg.db_connections);
  sk::certd::SnapshotSet snapshots;
  sk::certd::SnapshotLoader loader{db, snapshots};
  sk::certd::RateLimiter limiter;
  sk::certd::RecognitionPool recognition{cfg.recog_threads, cfg.recog_queue, sk::recog::recognize};
  // Username для кнопок open_app — из GET /me (bot_identity.hpp): неверный web_app MAX отвергает всё
  // сообщение.
  std::string bot_username = cfg.max_bot_username;
  if (cfg.bot_enabled() && sk::maxapi::HttpBotApi::tls_available()) {
    sk::maxapi::HttpBotApi probe{
        sk::maxapi::BotApiConfig{.base_url = cfg.max_api_base_url, .token = cfg.max_bot_token, .threads = 1}};
    auto identity = drogon::sync_wait(sk::certd::resolve_bot_username(&probe, cfg.max_bot_username));
    if (!identity.warning.empty()) {
      LOG_WARN << identity.warning;
    }
    bot_username = std::move(identity.username);
    LOG_INFO << "кнопки open_app: " << (bot_username.empty() ? "выключены" : "web_app=" + bot_username);
  }
  const sk::certd::bot::CardOptions card{.open_app = !bot_username.empty()};
  // Без токена бота уведомления фиксируются (статусы портфеля, строки notification), но в outbox не пишутся:
  // отправлять их некому.
  sk::certd::NotifyService notify{db, [card, enabled = cfg.bot_enabled()](const sk::certd::ChangeNotice& n) {
                                    return enabled ? sk::certd::bot::render_change_notice(n, card)
                                                   : std::vector<sk::maxapi::OutgoingMessage>{};
                                  }};
  sk::certd::DomainServiceImpl domain{
      db,      snapshots, recognition,
      limiter, notify,    [] {
                                        return sk::certd::moscow_date(std::chrono::system_clock::now()); }};
  sk::certd::JobQueue jobs{db};
  sk::certd::JobRunner runner{jobs};
  const auto handlers = std::make_shared<JobHandlers>(db, snapshots, notify, jobs);
  runner.on(std::string{sk::certd::kJobNotifyChanges},
            [handlers](std::string payload) { return handlers->notify_changes(std::move(payload)); });
  runner.on(std::string{sk::certd::kJobCleanup},
            [handlers](std::string payload) { return handlers->cleanup(std::move(payload)); });

  const auto health = std::make_shared<HealthEndpoint>(db, snapshots);
  app.registerHandler("/healthz",
                      [health](drogon::HttpRequestPtr req) { return health->handle(std::move(req)); },
                      {drogon::Get});
  sk::certd::RestApi::register_routes(
      app,
      std::make_shared<sk::certd::RestApi>(
          domain, sk::certd::AuthConfig{.bot_token = cfg.max_bot_token, .dev_user_id = cfg.dev_user_id}));
  if (cfg.dev_user_id_ignored) {
    LOG_WARN << "CERTD_DEV_USER_ID проигнорирован: задан MAX_BOT_TOKEN, вход только по initData (ADR-0013)";
  }
  if (cfg.dev_user_id) {
    LOG_WARN << "CERTD_DEV_USER_ID: мини-приложение без MAX работает от пользователя " << *cfg.dev_user_id
             << " (ADR-0013, только локально)";
  }

  // Бот и отправитель outbox — только при заданном токене.
  std::optional<sk::maxapi::HttpBotApi> bot_api;
  std::optional<sk::certd::PgOutbox> outbox;
  std::optional<sk::certd::PgInboundLog> inbound;
  std::optional<sk::certd::PgDialogStore> dialogs;
  std::optional<sk::certd::bot::Bot> bot;
  std::optional<sk::certd::Webhook> webhook;
  std::optional<sk::maxapi::SendLimiter> send_limiter;
  std::optional<sk::certd::OutboxSender> sender;
  if (cfg.bot_enabled() && !sk::maxapi::HttpBotApi::tls_available()) {
    // Без TLS токен ушёл бы открытым текстом, а MAX ответил бы 400 (ADR-0015).
    std::cerr << "certd: libcurl собран без TLS — бот не может обращаться к API MAX\n";
    return 2;
  }
  if (cfg.bot_enabled()) {
    bot_api.emplace(sk::maxapi::BotApiConfig{
        .base_url = cfg.max_api_base_url, .token = cfg.max_bot_token, .bot_username = bot_username});
    outbox.emplace(db);
    inbound.emplace(db);
    dialogs.emplace(db);
    bot.emplace(domain, *outbox, *dialogs, *bot_api, sk::certd::bot::BotConfig{.card = card});
    webhook.emplace(*bot, *inbound, cfg.max_webhook_secret);
    send_limiter.emplace();
    sender.emplace(db, *bot_api, *send_limiter);
    auto* hook = &*webhook;
    app.registerHandler("/max/webhook",
                        [hook](drogon::HttpRequestPtr req) { return hook->handle(std::move(req)); },
                        {drogon::Post});
  } else {
    LOG_WARN << "MAX_BOT_TOKEN не задан: бот выключен, работают /healthz, REST и мини-приложение";
  }

  Background bg;
  bg.loader = &loader;
  bg.jobs = &jobs;
  bg.runner = &runner;
  bg.sender = sender ? &*sender : nullptr;

  // Новая версия данных — по NOTIFY от ingest (C8); слушатель сам переподключается и повторяет LISTEN.
  const auto listener = drogon::orm::DbListener::newPgListener(cfg.pg_conninfo, app.getLoop());
  if (listener) {
    listener->listen("snapshot_ready", [&bg](const std::string& /*channel*/, const std::string& payload) {
      LOG_INFO << "NOTIFY snapshot_ready " << payload;
      trigger_refresh(&bg);
    });
  } else {
    LOG_WARN << "LISTEN недоступен: снапшоты подхватываются только опросом";
  }

  app.getLoop()->runAfter(0.0, [&bg, &jobs] {
    trigger_refresh(&bg);
    drogon::async_run([&jobs] { return schedule_cleanup(&jobs); });
    if (bg.sender != nullptr) {
      drogon::async_run([&bg] { return recover_outbox(bg.sender); });
    }
  });
  app.getLoop()->runEvery(kSnapshotPollSeconds, [&bg] { trigger_refresh(&bg); });
  app.getLoop()->runEvery(kJobPollSeconds, [&bg] {
    if (!bg.running_jobs.exchange(true)) {
      drogon::async_run([&bg] { return run_jobs(&bg); });
    }
  });
  if (bg.sender != nullptr) {
    app.getLoop()->runEvery(kOutboxPollSeconds, [&bg] {
      if (!bg.sending.exchange(true)) {
        drogon::async_run([&bg] { return drain_outbox(&bg); });
      }
    });
  }

  sk::certd::install_allow_header(app);  // после всех registerHandler: RFC 9110 требует Allow в ответе 405
  LOG_INFO << "certd: порт " << cfg.port << ", статика " << cfg.web_root.string();
  // Файлы пользователей на диск не сохраняются (АРХ §6). Временный каталог Drogon — в /tmp,
  // иначе он создаётся в рабочем каталоге, куда у непривилегированного пользователя нет прав.
  app.setDocumentRoot(cfg.web_root.string())
      .setUploadPath((std::filesystem::temp_directory_path() / "certd-uploads").string())
      .setUnicodeEscapingInJson(false)
      .setClientMaxBodySize(kMaxBodyBytes)
      .setClientMaxMemoryBodySize(kMaxBodyBytes)
      .setThreadNum(cfg.threads)
      .addListener("0.0.0.0", cfg.port)
      .run();
  return 0;
}

}  // namespace

int main() {
  // Журнал trantor пишет в stdout; вне терминала (docker compose logs) stdout буферизован блоками по 4 КиБ, и
  // строки появлялись бы только при остановке. Построчная буферизация — каждая запись видна сразу.
  if (std::setvbuf(stdout, nullptr, _IOLBF, 0) != 0) {
    std::cerr << "certd: не удалось включить построчный вывод журнала\n";
  }
  try {
    return run();
  } catch (const std::exception& e) {
    std::cerr << "certd: неперехваченное исключение: " << e.what() << '\n';
  } catch (...) {
    std::cerr << "certd: неперехваченное исключение неизвестного типа\n";
  }
  return 1;
}
