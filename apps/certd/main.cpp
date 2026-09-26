/// @file main.cpp
/// @brief Точка входа `certd`: конфиг, PostgreSQL, `/healthz`, статика мини-приложения.
#include <drogon/drogon.h>

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

#include "config.hpp"
#include "health.hpp"

namespace {

/// Обработчик `/healthz`. Корутина — метод класса, а не лямбда с захватом:
/// захваты лямбды-корутины висят после первой приостановки.
class HealthEndpoint {
 public:
  explicit HealthEndpoint(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

  drogon::Task<drogon::HttpResponsePtr> handle(drogon::HttpRequestPtr /*req*/) const {
    bool db_ok = true;
    try {
      co_await db_->execSqlCoro("SELECT 1");
    } catch (const drogon::orm::DrogonDbException& e) {
      LOG_WARN << "healthz: БД недоступна: " << e.base().what();
      db_ok = false;
    }
    const auto report = sk::certd::evaluate_health({.db_ok = db_ok});
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(report.http_status));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(report.body);
    co_return resp;
  }

 private:
  drogon::orm::DbClientPtr db_;
};

}  // namespace

namespace {

int run() {
  auto cfg = sk::certd::load_config(sk::certd::process_env);
  if (!cfg) {
    std::cerr << "certd: ошибка конфигурации: " << cfg.error().detail << '\n';
    return 2;
  }
  const auto& c = cfg.value();

  auto db = drogon::orm::DbClient::newPgClient(c.pg_conninfo, c.db_connections);
  auto health = std::make_shared<HealthEndpoint>(db);

  drogon::app().registerHandler(
      "/healthz", [health](drogon::HttpRequestPtr req) { return health->handle(std::move(req)); },
      {drogon::Get});

  LOG_INFO << "certd: порт " << c.port << ", статика " << c.web_root.string();
  // Файлы пользователей на диск не сохраняются (АРХ §6). Временный каталог Drogon — в /tmp,
  // иначе он создаётся в рабочем каталоге, куда у непривилегированного пользователя нет прав.
  drogon::app()
      .setDocumentRoot(c.web_root.string())
      .setUploadPath((std::filesystem::temp_directory_path() / "certd-uploads").string())
      .setThreadNum(c.threads)
      .addListener("0.0.0.0", c.port)
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
