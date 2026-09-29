#include <drogon/utils/coroutine.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

#include <json/json.h>
#include <trantor/net/EventLoopThreadPool.h>

#include "curl_http.hpp"
#include "json_parse.hpp"
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

struct HttpBotApi::Impl {
  explicit Impl(std::size_t threads) : pool(threads, "maxapi-http") { pool.start(); }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  ~Impl() {
    for (auto* loop : pool.getLoops()) {
      loop->quit();
    }
    pool.wait();
  }

  /// Блокирующий запрос в пуле; корутина ждёт через co_await и продолжается на потоке пула.
  drogon::Task<detail::HttpResponse> run(detail::HttpRequest request) {
    std::function<detail::HttpResponse()> task = [request = std::move(request)] {
      return detail::perform(request);
    };
    co_return co_await drogon::queueInLoopCoro<detail::HttpResponse>(pool.getNextLoop(), std::move(task));
  }

  trantor::EventLoopThreadPool pool;
};

HttpBotApi::HttpBotApi(BotApiConfig config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>(std::max<std::size_t>(1, config_.threads))) {
}

HttpBotApi::~HttpBotApi() = default;

bool HttpBotApi::tls_available() noexcept {
  return detail::tls_available();
}

drogon::Task<Result<Ok>> HttpBotApi::post(std::string path_and_query, std::string body) const {
  detail::HttpRequest req{.method = "POST",
                          .url = config_.base_url + path_and_query,
                          .headers = {"Authorization: " + config_.token, "Content-Type: application/json"},
                          .body = std::move(body),
                          .timeout_seconds = config_.timeout_seconds,
                          .max_response_bytes = std::size_t{1024} * 1024,
                          // http:// — только если так задан base_url (тесты с локальным сервером).
                          .allow_http = config_.base_url.starts_with("http://"),
                          .ca_file = config_.ca_file};
  const auto resp = co_await impl_->run(std::move(req));
  if (resp.status == 0) {
    co_return Error{ErrorCode::kInternal, "MAX API недоступен: " + resp.error};
  }
  if (resp.status != 200) {
    co_return from_status(static_cast<int>(resp.status), resp.body);
  }
  // `/answers` отвечает 200 и `success: false` при логической ошибке.
  Json::Value json;
  if (detail::parse_json(resp.body, json) && json.isObject() && json["success"].isBool() &&
      !json["success"].asBool()) {
    std::string message = "без пояснения";
    if (json["message"].isString()) {
      message = json["message"].asString();
    }
    co_return Error{ErrorCode::kInvalidArgument, "MAX API: " + message};
  }
  co_return Ok{};
}

Result<BotInfo> parse_bot_info(std::string_view json) {
  Json::Value v;
  if (!detail::parse_json(json, v) || !v.isObject() || !v["user_id"].isInt64() ||
      !(v["username"].isNull() || v["username"].isString())) {
    return Error{ErrorCode::kInvalidArgument, "GET /me: ожидается объект с user_id и username"};
  }
  BotInfo info{.user_id = v["user_id"].asInt64()};
  if (v["username"].isString() && !v["username"].asString().empty()) {
    info.username = v["username"].asString();
  }
  return info;
}

drogon::Task<Result<BotInfo>> HttpBotApi::get_me() {
  detail::HttpRequest req{.method = "GET",
                          .url = config_.base_url + "/me",
                          .headers = {"Authorization: " + config_.token},
                          .timeout_seconds = config_.timeout_seconds,
                          .max_response_bytes = std::size_t{64} * 1024,
                          .allow_http = config_.base_url.starts_with("http://"),
                          .ca_file = config_.ca_file};
  const auto resp = co_await impl_->run(std::move(req));
  if (resp.status == 0) {
    co_return Error{ErrorCode::kInternal, "MAX API недоступен: " + resp.error};
  }
  if (resp.status != 200) {
    co_return from_status(static_cast<int>(resp.status), resp.body);
  }
  co_return parse_bot_info(resp.body);
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
  detail::HttpRequest req{.url = std::move(url),
                          .timeout_seconds = config_.timeout_seconds,
                          .max_response_bytes = max_bytes,
                          .allow_http = config_.allow_http_downloads,
                          .follow_redirects = true,
                          .ca_file = config_.ca_file};
  const auto resp = co_await impl_->run(std::move(req));
  if (resp.too_large) {
    co_return Error{ErrorCode::kFileTooLarge, "вложение больше лимита"};
  }
  if (resp.status == 0) {
    co_return Error{ErrorCode::kInternal, "вложение не скачалось: " + resp.error};
  }
  if (resp.status != 200) {
    co_return from_status(static_cast<int>(resp.status), "вложение");
  }
  std::vector<std::byte> out(resp.body.size());
  std::memcpy(out.data(), resp.body.data(), resp.body.size());
  co_return out;
}

}  // namespace sk::maxapi
