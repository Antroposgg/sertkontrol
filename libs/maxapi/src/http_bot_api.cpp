#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <cstring>
#include <memory>
#include <utility>

#include <json/json.h>

#include "sertkontrol/maxapi/auth.hpp"
#include "sertkontrol/maxapi/bot_api.hpp"

namespace sk::maxapi {

namespace {

Error from_status(int status, const std::string& body) {
  const auto detail = "MAX API HTTP " + std::to_string(status) + ": " + body.substr(0, 300);
  if (status == 429) {
    return Error{ErrorCode::kRateLimited, detail};
  }
  if (status == 401) {
    return Error{ErrorCode::kUnauthorized, detail};
  }
  if (status >= 500 || status == 0) {
    return Error{ErrorCode::kInternal, detail};
  }
  return Error{ErrorCode::kInvalidArgument, detail};
}

}  // namespace

std::optional<SplitUrl> split_url(std::string_view url) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return std::nullopt;
  }
  const auto scheme = url.substr(0, scheme_end);
  if (scheme != "https" && scheme != "http") {
    return std::nullopt;
  }
  const auto host_start = scheme_end + 3;
  const auto path_start = url.find('/', host_start);
  const auto host = url.substr(
      host_start, path_start == std::string_view::npos ? std::string_view::npos : path_start - host_start);
  if (host.empty() || host.find_first_of("@ \t\r\n") != std::string_view::npos) {
    return std::nullopt;
  }
  return SplitUrl{.origin = std::string{url.substr(0, host_start)} + std::string{host},
                  .path = path_start == std::string_view::npos ? "/" : std::string{url.substr(path_start)}};
}

HttpBotApi::HttpBotApi(BotApiConfig config) : config_(std::move(config)) {
}

drogon::Task<Result<Ok>> HttpBotApi::post(std::string path_and_query, std::string body) const {
  auto client = drogon::HttpClient::newHttpClient(config_.base_url);
  auto req = drogon::HttpRequest::newHttpRequest();
  req->setMethod(drogon::Post);
  req->setPathEncode(false);  // query уже закодирован
  req->setPath(path_and_query);
  req->addHeader("Authorization", config_.token);
  req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  req->setBody(std::move(body));
  drogon::HttpResponsePtr resp;
  try {
    resp = co_await client->sendRequestCoro(req, config_.timeout_seconds);
  } catch (const std::exception& e) {
    co_return Error{ErrorCode::kInternal, std::string{"MAX API недоступен: "} + e.what()};
  }
  const auto status = static_cast<int>(resp->getStatusCode());
  const std::string resp_body{resp->getBody()};
  if (status != 200) {
    co_return from_status(status, resp_body);
  }
  // `/answers` отвечает 200 и `success: false` при логической ошибке.
  if (const auto json = resp->getJsonObject();
      json && json->isMember("success") && !(*json)["success"].asBool()) {
    co_return Error{ErrorCode::kInvalidArgument, "MAX API: " + (*json)["message"].asString()};
  }
  co_return Ok{};
}

drogon::Task<Result<Ok>> HttpBotApi::send_message(OutgoingMessage msg) {
  if (auto v = validate(msg); !v) {
    co_return v.error();
  }
  co_return co_await post("/messages?user_id=" + std::to_string(msg.max_user_id),
                          to_new_message_body(msg, config_.bot_username));
}

drogon::Task<Result<Ok>> HttpBotApi::answer_callback(std::string callback_id, std::string notification) {
  Json::Value body{Json::objectValue};
  body["notification"] = notification;
  Json::StreamWriterBuilder b;
  b["indentation"] = "";
  b["emitUTF8"] = true;
  co_return co_await post("/answers?callback_id=" + percent_encode(callback_id), Json::writeString(b, body));
}

drogon::Task<Result<std::vector<std::byte>>> HttpBotApi::download(std::string url, std::size_t max_bytes) {
  const auto split = split_url(url);
  if (!split || (!config_.allow_http_downloads && !url.starts_with("https://"))) {
    co_return Error{ErrorCode::kInvalidArgument, "вложение: недопустимый URL"};
  }
  auto client = drogon::HttpClient::newHttpClient(split->origin);
  auto req = drogon::HttpRequest::newHttpRequest();
  req->setMethod(drogon::Get);
  req->setPathEncode(false);
  req->setPath(split->path);
  drogon::HttpResponsePtr resp;
  try {
    resp = co_await client->sendRequestCoro(req, config_.timeout_seconds);
  } catch (const std::exception& e) {
    co_return Error{ErrorCode::kInternal, std::string{"вложение не скачалось: "} + e.what()};
  }
  const auto status = static_cast<int>(resp->getStatusCode());
  if (status != 200) {
    co_return from_status(status, "вложение");
  }
  const auto body = resp->getBody();
  if (body.size() > max_bytes) {
    co_return Error{ErrorCode::kFileTooLarge, "вложение больше лимита"};
  }
  std::vector<std::byte> out(body.size());
  std::memcpy(out.data(), body.data(), body.size());
  co_return out;
}

}  // namespace sk::maxapi
