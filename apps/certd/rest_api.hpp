/// @file rest_api.hpp
/// @brief REST `/api/v1` мини-приложения (C7, `openapi.yaml`). Обработчики тонкие: запрос → `DomainService` →
/// JSON.
#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "domain.hpp"

namespace sk::certd {

/// Как аутентифицировать запросы мини-приложения.
struct AuthConfig {
  std::string bot_token{};  ///< Ключ проверки initData (ADR-0006).
  std::optional<std::int64_t> dev_user_id{};  ///< Только без токена: локальный запуск без MAX (ADR-0013).
  std::function<std::chrono::system_clock::time_point()> now{[] { return std::chrono::system_clock::now(); }};
};

/// Обработчики `/api/v1`. Каждый запрос несёт `X-Max-Init-Data`; пользователь берётся только из неё.
class RestApi {
 public:
  RestApi(DomainService& domain, AuthConfig auth) : domain_(domain), auth_(std::move(auth)) {}

  drogon::Task<drogon::HttpResponsePtr> me(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> check(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> check_file(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> list_portfolio(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> add_to_portfolio(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> remove_from_portfolio(drogon::HttpRequestPtr req, std::string id);
  drogon::Task<drogon::HttpResponsePtr> data_status(drogon::HttpRequestPtr req);

  /// Регистрирует маршруты `/api/v1/*`. `api` должен жить до остановки приложения.
  static void register_routes(drogon::HttpAppFramework& app, const std::shared_ptr<RestApi>& api);

  /// Пользователь запроса: initData из `X-Max-Init-Data` или dev-пользователь (ADR-0013).
  [[nodiscard]] Result<UserContext> authenticate(const drogon::HttpRequestPtr& req) const;

 private:
  DomainService& domain_;
  AuthConfig auth_;
};

/// Ответ `application/problem+json` (RFC 9457).
[[nodiscard]] drogon::HttpResponsePtr problem_response(const Error& error);

}  // namespace sk::certd
