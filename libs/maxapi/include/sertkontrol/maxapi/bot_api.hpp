/// @file bot_api.hpp
/// @brief Клиент Bot API MAX: отправка сообщений, ответы на кнопки, скачивание вложений.
///
/// Всё, что не удалось проверить без настоящего бота (скачивание по `payload.url`), живёт за этим
/// интерфейсом и описано в «известных ограничениях» README.
#pragma once

#include <drogon/utils/coroutine.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sertkontrol/maxapi/message.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::maxapi {

/// Ошибки: `kRateLimited` (429), `kInternal` (5xx, сеть, таймаут — можно повторить), `kUnauthorized` (401),
/// `kInvalidArgument` (прочие 4xx или `success: false`), `kFileTooLarge` (вложение больше лимита).
class BotApi {
 public:
  BotApi() = default;
  BotApi(const BotApi&) = delete;
  BotApi& operator=(const BotApi&) = delete;
  BotApi(BotApi&&) = delete;
  BotApi& operator=(BotApi&&) = delete;
  virtual ~BotApi() = default;

  /// `POST /messages?user_id=`.
  virtual drogon::Task<Result<Ok>> send_message(OutgoingMessage msg) = 0;
  /// `POST /answers?callback_id=` с одноразовым уведомлением пользователю.
  virtual drogon::Task<Result<Ok>> answer_callback(std::string callback_id, std::string notification) = 0;
  /// Скачивает вложение по `payload.url` не больше `max_bytes` байт.
  virtual drogon::Task<Result<std::vector<std::byte>>> download(std::string url, std::size_t max_bytes) = 0;
};

/// Настройки HTTP-клиента.
struct BotApiConfig {
  std::string base_url{"https://platform-api2.max.ru"};  ///< `platform-api.max.ru` устарел (dev.max.ru).
  std::string token{};
  std::string bot_username{};  ///< Для кнопок `open_app` (`web_app`).
  double timeout_seconds{10.0};
  bool allow_http_downloads{false};  ///< Только для тестов с локальным сервером.
  std::string ca_file{};  ///< Свой CA-файл (тесты с локальным TLS); пусто — системное хранилище.
  std::size_t threads{2};  ///< Потоки блокирующих HTTP-запросов.
};

/// Реализация на libcurl в отдельном пуле потоков (ADR-0014: trantor из apt без TLS). Сертификат сервера
/// проверяется по системному хранилищу (там корень Минцифры, ADR-0012). Токен передаётся только в
/// `Authorization` и только на `base_url`: при скачивании вложений он не отправляется.
class HttpBotApi final : public BotApi {
 public:
  explicit HttpBotApi(BotApiConfig config);
  HttpBotApi(const HttpBotApi&) = delete;
  HttpBotApi& operator=(const HttpBotApi&) = delete;
  HttpBotApi(HttpBotApi&&) = delete;
  HttpBotApi& operator=(HttpBotApi&&) = delete;
  ~HttpBotApi() override;

  /// libcurl собран с TLS — без этого бот не может обращаться к `https://platform-api2.max.ru`.
  [[nodiscard]] static bool tls_available() noexcept;
  drogon::Task<Result<Ok>> send_message(OutgoingMessage msg) override;
  drogon::Task<Result<Ok>> answer_callback(std::string callback_id, std::string notification) override;
  drogon::Task<Result<std::vector<std::byte>>> download(std::string url, std::size_t max_bytes) override;

 private:
  struct Impl;
  [[nodiscard]] drogon::Task<Result<Ok>> post(std::string path_and_query, std::string body) const;
  BotApiConfig config_;
  std::unique_ptr<Impl> impl_;
};

/// Делит абсолютный URL на origin (`https://host[:port]`) и путь с query. `nullopt` — не http(s)-URL.
struct SplitUrl {
  std::string origin{};
  std::string path{};
};
[[nodiscard]] std::optional<SplitUrl> split_url(std::string_view url);

}  // namespace sk::maxapi
