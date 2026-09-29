#include "rest_api.hpp"

#include <drogon/MultiPart.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <vector>

#include "json_views.hpp"
#include "media.hpp"
#include "sertkontrol/maxapi/auth.hpp"

namespace sk::certd {

namespace {

drogon::HttpResponsePtr json_response(const Json::Value& body,
                                      drogon::HttpStatusCode status = drogon::k200OK) {
  auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
  resp->setStatusCode(status);
  return resp;
}

std::optional<std::int64_t> parse_int(std::string_view s) {
  std::int64_t v = 0;
  const auto* end = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(s.data(), end, v);
  if (s.empty() || ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return v;
}

Error bad_request(std::string detail) {
  return Error{ErrorCode::kInvalidArgument, std::move(detail)};
}

/// Параметр запроса вне схемы C7 — 400, а не молчаливое игнорирование: опечатка в фильтре (`?stauts=`) иначе
/// вернула бы весь портфель как «отфильтрованный» (находка schemathesis, АРХ §10 «Интеграция»).
std::optional<Error> unknown_query_parameter(const drogon::HttpRequestPtr& req,
                                             std::initializer_list<std::string_view> known) {
  for (const auto& [name, value] : req->getParameters()) {
    if (std::find(known.begin(), known.end(), name) == known.end()) {
      return bad_request("неизвестный параметр " + name);
    }
  }
  return std::nullopt;
}

}  // namespace

drogon::HttpResponsePtr problem_response(const Error& error) {
  auto resp =
      json_response(problem_json(error), static_cast<drogon::HttpStatusCode>(http_status(error.code)));
  resp->setContentTypeString("application/problem+json; charset=utf-8");
  return resp;
}

Result<UserContext> RestApi::authenticate(const drogon::HttpRequestPtr& req) const {
  const auto& raw = req->getHeader("X-Max-Init-Data");
  if (raw.empty() && auth_.bot_token.empty() && auth_.dev_user_id) {
    return UserContext{.max_user_id = *auth_.dev_user_id, .channel = Channel::kApp};
  }
  auto init = maxapi::validate_init_data(raw, auth_.bot_token, auth_.now());
  if (!init) {
    return init.error();
  }
  return UserContext{.max_user_id = init.value().user_id, .channel = Channel::kApp};
}

drogon::Task<Result<UserContext>> RestApi::authorize(drogon::HttpRequestPtr req) {
  auto user = authenticate(req);
  if (!user) {
    co_return user.error();
  }
  // Без согласия данные не обрабатываются (АРХ §10) — так же, как в боте.
  const auto me = co_await domain_.me(user.value());
  if (!me) {
    co_return me.error();
  }
  if (!me.value().consented) {
    co_return Error{ErrorCode::kConsentRequired,
                    "нужно согласие на обработку данных: POST /api/v1/me/consent"};
  }
  co_return user;
}

drogon::Task<drogon::HttpResponsePtr> RestApi::consent(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto r = co_await domain_.give_consent(user.value());
  if (!r) {
    co_return problem_response(r.error());
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  co_return resp;
}

drogon::Task<drogon::HttpResponsePtr> RestApi::me(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto r = co_await domain_.me(user.value());
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::check(drogon::HttpRequestPtr req) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  if (const auto e = unknown_query_parameter(req, {"number"})) {
    co_return problem_response(*e);
  }
  const auto number = req->getParameter("number");
  if (number.empty()) {
    co_return problem_response(bad_request("параметр number обязателен"));
  }
  const auto r = co_await domain_.check_text(user.value(), number);
  if (!r) {
    co_return problem_response(r.error());
  }
  co_return json_response(to_json(r.value().verdicts.front()));
}

drogon::Task<drogon::HttpResponsePtr> RestApi::check_file(drogon::HttpRequestPtr req) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  drogon::MultiPartParser parser;
  if (parser.parse(req) != 0) {
    co_return problem_response(bad_request("ожидается multipart/form-data с полем file"));
  }
  // Именно поле `file` (openapi.yaml), а не первая попавшаяся часть.
  const auto& files = parser.getFiles();
  const auto file =
      std::ranges::find_if(files, [](const drogon::HttpFile& f) { return f.getItemName() == "file"; });
  if (file == files.end()) {
    co_return problem_response(bad_request("ожидается multipart/form-data с полем file"));
  }
  const auto content = file->fileContent();
  FileUpload upload;
  upload.bytes.resize(content.size());
  std::memcpy(upload.bytes.data(), content.data(), content.size());
  const auto type = detect_media_type(upload.bytes);
  if (!type) {
    co_return problem_response(Error{ErrorCode::kUnsupportedMediaType, "поддерживаются PDF-выписки"});
  }
  upload.type = *type;
  const auto r = co_await domain_.check_file(user.value(), std::move(upload));
  if (!r) {
    co_return problem_response(r.error());
  }
  Json::Value out{Json::arrayValue};
  for (const auto& v : r.value().verdicts) {
    out.append(to_json(v));
  }
  co_return json_response(out);
}

drogon::Task<drogon::HttpResponsePtr> RestApi::list_portfolio(drogon::HttpRequestPtr req) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  if (const auto e = unknown_query_parameter(req, {"status", "supplier_inn", "cursor", "limit"})) {
    co_return problem_response(*e);
  }
  PortfolioFilter filter;
  // Переданный параметр проверяется и пустым: `?status=`, `?cursor=` — ошибка, а не «без фильтра»
  // (openapi.yaml).
  const auto& params = req->getParameters();
  if (params.contains("status")) {
    const auto s = req->getParameter("status");
    filter.status = snapshot::status_from_string(s);
    if (!filter.status) {
      co_return problem_response(bad_request("неизвестный status: " + s));
    }
  }
  if (params.contains("supplier_inn")) {
    const auto inn = req->getParameter("supplier_inn");
    if (inn.size() != 10 && inn.size() != 12) {
      co_return problem_response(bad_request("supplier_inn: 10 или 12 цифр"));
    }
    if (!std::ranges::all_of(inn, [](char c) { return c >= '0' && c <= '9'; })) {
      co_return problem_response(bad_request("supplier_inn: 10 или 12 цифр"));
    }
    filter.supplier_inn = inn;
  }
  if (params.contains("cursor")) {
    // Схема: '^[0-9]{1,18}$' — без знака и без переполнения int64.
    const auto c = req->getParameter("cursor");
    const bool digits = !c.empty() && c.size() <= 18 &&
                        std::ranges::all_of(c, [](char ch) { return ch >= '0' && ch <= '9'; });
    filter.cursor = digits ? parse_int(c) : std::nullopt;
    if (!filter.cursor) {
      co_return problem_response(bad_request("некорректный cursor"));
    }
  }
  if (params.contains("limit")) {
    const auto limit = parse_int(req->getParameter("limit"));
    if (!limit || *limit < 1 || *limit > 100) {
      co_return problem_response(bad_request("limit: 1..100"));
    }
    filter.limit = static_cast<std::size_t>(*limit);
  }
  const auto r = co_await domain_.list_portfolio(user.value(), std::move(filter));
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::add_to_portfolio(drogon::HttpRequestPtr req) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto body = req->getJsonObject();
  if (!body || !body->isObject() || !(*body)["number"].isString()) {
    co_return problem_response(bad_request("ожидается JSON {number, sku?, supplier_inn?}"));
  }
  AddRequest request{.number = (*body)["number"].asString()};
  for (const auto* field : {"sku", "supplier_inn"}) {
    const auto& v = (*body)[field];
    if (!v.isNull() && !v.isString()) {
      co_return problem_response(bad_request(std::string{field} + " должен быть строкой"));
    }
  }
  if ((*body)["sku"].isString()) {
    request.sku = (*body)["sku"].asString();
  }
  if ((*body)["supplier_inn"].isString()) {
    request.supplier_inn = (*body)["supplier_inn"].asString();
  }
  const auto r = co_await domain_.add_to_portfolio(user.value(), std::move(request));
  co_return r ? json_response(to_json(r.value()), drogon::k201Created) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::remove_from_portfolio(drogon::HttpRequestPtr req,
                                                                     std::string id) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto item_id = parse_int(id);
  if (!item_id) {
    co_return problem_response(Error{ErrorCode::kNotFound, "запись не найдена"});
  }
  const auto r = co_await domain_.remove_from_portfolio(user.value(), *item_id);
  if (!r) {
    co_return problem_response(r.error());
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  co_return resp;
}

drogon::Task<drogon::HttpResponsePtr> RestApi::data_status(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto r = co_await domain_.data_status(user.value());
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::history(drogon::HttpRequestPtr req) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  if (const auto e = unknown_query_parameter(req, {"number"})) {
    co_return problem_response(*e);
  }
  const auto number = req->getParameter("number");
  if (number.empty()) {
    co_return problem_response(bad_request("параметр number обязателен"));
  }
  const auto r = co_await domain_.history(user.value(), number);
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::simulate_update(drogon::HttpRequestPtr req) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto r = co_await domain_.simulate_update(user.value());
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::reset_demo(drogon::HttpRequestPtr req) {
  const auto user = co_await authorize(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto r = co_await domain_.reset_demo(user.value());
  if (!r) {
    co_return problem_response(r.error());
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  co_return resp;
}

void RestApi::register_routes(drogon::HttpAppFramework& app, const std::shared_ptr<RestApi>& api) {
  using drogon::Delete;
  using drogon::Get;
  using drogon::HttpRequestPtr;
  using drogon::Post;
  // Лямбды не корутины: возвращают Task метода, поэтому захват api безопасен.
  app.registerHandler("/api/v1/me", [api](HttpRequestPtr req) { return api->me(std::move(req)); }, {Get});
  app.registerHandler("/api/v1/me/consent",
                      [api](HttpRequestPtr req) { return api->consent(std::move(req)); }, {Post});
  app.registerHandler("/api/v1/check", [api](HttpRequestPtr req) { return api->check(std::move(req)); },
                      {Get});
  app.registerHandler("/api/v1/check/file",
                      [api](HttpRequestPtr req) { return api->check_file(std::move(req)); }, {Post});
  app.registerHandler("/api/v1/portfolio",
                      [api](HttpRequestPtr req) { return api->list_portfolio(std::move(req)); }, {Get});
  app.registerHandler("/api/v1/portfolio",
                      [api](HttpRequestPtr req) { return api->add_to_portfolio(std::move(req)); }, {Post});
  app.registerHandler("/api/v1/portfolio/{1}",
                      [api](HttpRequestPtr req, std::string id) {
                        return api->remove_from_portfolio(std::move(req), std::move(id));
                      },
                      {Delete});
  // Drogon 1.8.7 на маршруте с параметром при чужом методе отвечает 404, а не 405 (RFC 9110 §15.5.6).
  app.registerHandler(std::string{kExplicit405Path},
                      [](const HttpRequestPtr&,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string&) {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::k405MethodNotAllowed);
                        resp->addHeader("Allow", "DELETE");
                        std::move(callback)(resp);
                      },
                      {kExplicit405Methods.begin(), kExplicit405Methods.end()});
  app.registerHandler("/api/v1/data-status",
                      [api](HttpRequestPtr req) { return api->data_status(std::move(req)); }, {Get});
  app.registerHandler("/api/v1/history", [api](HttpRequestPtr req) { return api->history(std::move(req)); },
                      {Get});
  app.registerHandler("/api/v1/demo/simulate-update",
                      [api](HttpRequestPtr req) { return api->simulate_update(std::move(req)); }, {Post});
  app.registerHandler("/api/v1/demo/reset",
                      [api](HttpRequestPtr req) { return api->reset_demo(std::move(req)); }, {Post});
}

namespace {

std::vector<std::string_view> segments(std::string_view path) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  while (start < path.size()) {
    const auto slash = path.find('/', start);
    const auto end = slash == std::string_view::npos ? path.size() : slash;
    if (end > start) {
      out.push_back(path.substr(start, end - start));
    }
    start = end + 1;
  }
  return out;
}

std::string_view method_name(drogon::HttpMethod m) {
  switch (m) {
    case drogon::Get:
      return "GET";
    case drogon::Post:
      return "POST";
    case drogon::Head:
      return "HEAD";
    case drogon::Put:
      return "PUT";
    case drogon::Delete:
      return "DELETE";
    case drogon::Options:
      return "OPTIONS";
    case drogon::Patch:
      return "PATCH";
    default:
      return "";
  }
}

bool matches(std::string_view pattern, std::string_view path) {
  const auto p = segments(pattern);
  const auto s = segments(path);
  if (p.size() != s.size()) {
    return false;
  }
  for (std::size_t i = 0; i < p.size(); ++i) {
    if (!(p[i].starts_with('{') && p[i].ends_with('}')) && p[i] != s[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool is_explicit_405(std::string_view pattern, drogon::HttpMethod method) noexcept {
  return pattern == kExplicit405Path &&
         std::ranges::find(kExplicit405Methods, method) != kExplicit405Methods.end();
}

std::string allowed_methods(std::string_view path, const std::vector<Route>& routes) {
  std::vector<std::string> names;
  for (const auto& r : routes) {
    if (!matches(r.pattern, path)) {
      continue;
    }
    std::string name{method_name(r.method)};
    if (name.empty()) {
      continue;
    }
    if (std::ranges::find(names, name) == names.end()) {
      names.push_back(std::move(name));
    }
  }
  std::ranges::sort(names);
  std::string out;
  for (const auto& n : names) {
    out += (out.empty() ? "" : ", ") + n;
  }
  return out;
}

void install_allow_header(drogon::HttpAppFramework& app) {
  auto routes = std::make_shared<std::vector<Route>>();
  for (const auto& [pattern, method, description] : app.getHandlersInfo()) {
    if (is_explicit_405(pattern, method)) {
      continue;
    }
    routes->push_back({.pattern = pattern, .method = method});
  }
  // PreSending, а не PostHandling: 405 маршрутизатора Drogon формирует сам, без обработчика.
  app.registerPreSendingAdvice(
      [routes](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
        if (resp->statusCode() == drogon::k405MethodNotAllowed && resp->getHeader("Allow").empty()) {
          if (auto allow = allowed_methods(req->path(), *routes); !allow.empty()) {
            resp->addHeader("Allow", std::move(allow));
          }
        }
      });
}

}  // namespace sk::certd
