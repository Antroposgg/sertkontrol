/// @file curl_http.hpp
/// @brief Блокирующий HTTP(S)-запрос через libcurl (внутренний заголовок libs/maxapi, ADR-0014).
///
/// Почему не `drogon::HttpClient`: trantor из apt Ubuntu 24.04 собран без TLS (`tlsBackend() == "None"`),
/// и Drogon для `https://` молча отправляет обычный HTTP на порт 443.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sk::maxapi::detail {

/// Параметры запроса.
struct HttpRequest {
  std::string method{"GET"};
  std::string url{};
  std::vector<std::string> headers{};  ///< `Имя: значение`.
  std::string body{};
  double timeout_seconds{10.0};
  std::size_t max_response_bytes{std::size_t{32} * 1024 * 1024};
  bool allow_http{false};  ///< Разрешить `http://` (только тесты с локальным сервером).
  bool follow_redirects{false};  ///< Только для скачивания вложений; переходы — лишь на https.
  std::string ca_file{};  ///< Свой CA-файл (тесты); пусто — системное хранилище.
};

/// Результат: `status == 0` — сетевая ошибка или TLS (`error`), `too_large` — ответ больше лимита.
struct HttpResponse {
  long status{0};
  std::string body{};
  std::string error{};
  bool too_large{false};
};

/// Выполняет запрос. Потокобезопасна; `curl_global_init` вызывается один раз.
[[nodiscard]] HttpResponse perform(const HttpRequest& request);

/// libcurl собран с поддержкой TLS.
[[nodiscard]] bool tls_available() noexcept;

}  // namespace sk::maxapi::detail
