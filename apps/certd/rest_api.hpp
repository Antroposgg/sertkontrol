/// @file rest_api.hpp
/// @brief REST `/api/v1` мини-приложения (C7, `openapi.yaml`). Обработчики тонкие: запрос → `DomainService` →
/// JSON.
#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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
  /// `POST /me/consent` — согласие на обработку данных (как кнопка «Согласен» в боте).
  drogon::Task<drogon::HttpResponsePtr> consent(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> check(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> check_file(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> list_portfolio(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> add_to_portfolio(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> remove_from_portfolio(drogon::HttpRequestPtr req, std::string id);
  drogon::Task<drogon::HttpResponsePtr> data_status(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> history(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> simulate_update(drogon::HttpRequestPtr req);
  drogon::Task<drogon::HttpResponsePtr> reset_demo(drogon::HttpRequestPtr req);

  /// Регистрирует маршруты `/api/v1/*`. `api` должен жить до остановки приложения.
  static void register_routes(drogon::HttpAppFramework& app, const std::shared_ptr<RestApi>& api);

  /// `authenticate` + согласие на обработку данных: без него — `kConsentRequired` (403, АРХ §10).
  drogon::Task<Result<UserContext>> authorize(drogon::HttpRequestPtr req);

  /// Пользователь запроса: initData из `X-Max-Init-Data` или dev-пользователь (ADR-0013).
  [[nodiscard]] Result<UserContext> authenticate(const drogon::HttpRequestPtr& req) const;

 private:
  DomainService& domain_;
  AuthConfig auth_;
};

/// Путь, на котором чужие методы отвечают 405 явным обработчиком: Drogon 1.8.7 на маршруте с параметром
/// отвечает на них 404 (RFC 9110 §15.5.6 требует 405). Такие маршруты не операции API и не входят в `Allow`.
inline constexpr std::string_view kExplicit405Path = "/api/v1/portfolio/{1}";
inline constexpr std::array<drogon::HttpMethod, 4> kExplicit405Methods{drogon::Get, drogon::Post, drogon::Put,
                                                                       drogon::Patch};

/// Маршрут — явный ответ 405 (см. `kExplicit405Path`), а не операция API.
[[nodiscard]] bool is_explicit_405(std::string_view pattern, drogon::HttpMethod method) noexcept;

/// Маршрут: шаблон пути Drogon (`/api/v1/portfolio/{1}`) и метод.
struct Route {
  std::string pattern{};
  drogon::HttpMethod method{drogon::Get};
};

/// Значение заголовка `Allow` для пути (RFC 9110 §15.5.6: ответ 405 обязан его содержать): методы всех
/// маршрутов, чей шаблон совпадает с путём (`{…}` — любой один сегмент), через «, ». Пусто — путь неизвестен.
[[nodiscard]] std::string allowed_methods(std::string_view path, const std::vector<Route>& routes);

/// Добавляет `Allow` ко всем ответам 405. Вызывать после регистрации всех маршрутов.
void install_allow_header(drogon::HttpAppFramework& app);

/// Ответ `application/problem+json` (RFC 9457).
[[nodiscard]] drogon::HttpResponsePtr problem_response(const Error& error);

}  // namespace sk::certd
