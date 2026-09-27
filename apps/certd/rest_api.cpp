#include "rest_api.hpp"

#include <drogon/MultiPart.h>

#include <charconv>
#include <cstring>

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

drogon::Task<drogon::HttpResponsePtr> RestApi::me(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto r = co_await domain_.me(user.value());
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::check(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
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
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  drogon::MultiPartParser parser;
  if (parser.parse(req) != 0 || parser.getFiles().empty()) {
    co_return problem_response(bad_request("ожидается multipart/form-data с полем file"));
  }
  const auto content = parser.getFiles().front().fileContent();
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
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  PortfolioFilter filter;
  if (const auto s = req->getParameter("status"); !s.empty()) {
    filter.status = snapshot::status_from_string(s);
    if (!filter.status) {
      co_return problem_response(bad_request("неизвестный status: " + s));
    }
  }
  if (const auto inn = req->getParameter("supplier_inn"); !inn.empty()) {
    filter.supplier_inn = inn;
  }
  if (const auto c = req->getParameter("cursor"); !c.empty()) {
    filter.cursor = parse_int(c);
    if (!filter.cursor) {
      co_return problem_response(bad_request("некорректный cursor"));
    }
  }
  if (const auto l = req->getParameter("limit"); !l.empty()) {
    const auto limit = parse_int(l);
    if (!limit || *limit < 1 || *limit > 100) {
      co_return problem_response(bad_request("limit: 1..100"));
    }
    filter.limit = static_cast<std::size_t>(*limit);
  }
  const auto r = co_await domain_.list_portfolio(user.value(), std::move(filter));
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::add_to_portfolio(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
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
  const auto user = authenticate(req);
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
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto number = req->getParameter("number");
  if (number.empty()) {
    co_return problem_response(bad_request("параметр number обязателен"));
  }
  const auto r = co_await domain_.history(user.value(), number);
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::simulate_update(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
  if (!user) {
    co_return problem_response(user.error());
  }
  const auto r = co_await domain_.simulate_update(user.value());
  co_return r ? json_response(to_json(r.value())) : problem_response(r.error());
}

drogon::Task<drogon::HttpResponsePtr> RestApi::reset_demo(drogon::HttpRequestPtr req) {
  const auto user = authenticate(req);
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
  app.registerHandler("/api/v1/data-status",
                      [api](HttpRequestPtr req) { return api->data_status(std::move(req)); }, {Get});
  app.registerHandler("/api/v1/history", [api](HttpRequestPtr req) { return api->history(std::move(req)); },
                      {Get});
  app.registerHandler("/api/v1/demo/simulate-update",
                      [api](HttpRequestPtr req) { return api->simulate_update(std::move(req)); }, {Post});
  app.registerHandler("/api/v1/demo/reset",
                      [api](HttpRequestPtr req) { return api->reset_demo(std::move(req)); }, {Post});
}

}  // namespace sk::certd
