#include "webhook.hpp"

#include <trantor/utils/Logger.h>

#include "rest_api.hpp"
#include "sertkontrol/maxapi/auth.hpp"

namespace sk::certd {

namespace {

drogon::HttpResponsePtr ok_response() {
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k200OK);
  resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  resp->setBody(R"({"ok":true})");
  return resp;
}

}  // namespace

drogon::Task<drogon::HttpResponsePtr> Webhook::handle(drogon::HttpRequestPtr req) {
  if (!maxapi::secret_matches(req->getHeader("X-Max-Bot-Api-Secret"), secret_)) {
    co_return problem_response(Error{ErrorCode::kUnauthorized, "неверный секрет webhook"});
  }
  auto update = maxapi::parse_update(req->body());
  if (!update) {
    LOG_WARN << "webhook: " << update.error().detail;
    co_return problem_response(update.error());
  }
  auto key = maxapi::dedup_key(update.value());
  const auto first = co_await inbound_.first_seen(key);
  if (!first) {
    // Не записали — пусть MAX повторит (до 10 попыток, dev.max.ru).
    co_return problem_response(first.error());
  }
  if (!first.value()) {
    co_return ok_response();  // повтор уже принятого события
  }
  drogon::async_run([this, u = std::move(update).value(), k = std::move(key)]() mutable {
    return process(std::move(u), std::move(k));
  });
  co_return ok_response();
}

drogon::Task<void> Webhook::process(maxapi::Update update, std::string dedup_key) {
  std::string error;
  try {
    error = co_await bot_.handle(std::move(update));
  } catch (const std::exception& e) {
    error = e.what();
  }
  if (!error.empty()) {
    LOG_WARN << "webhook " << dedup_key << ": " << error;
  }
  co_await inbound_.mark_processed(std::move(dedup_key), std::move(error));
}

}  // namespace sk::certd
