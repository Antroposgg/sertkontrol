/// @file auth.hpp
/// @brief Проверка подлинности запросов MAX: initData мини-приложения (ADR-0006) и секрет webhook (АРХ §10).
///
/// Алгоритм initData — официальный (dev.max.ru/docs/webapps/validation, сверено 26.09.2026):
/// разбить по `&`, ровно один `hash`, URL-декодировать значения, отсортировать по ключу, склеить `k=v` через
/// `\n`; `secret = HMAC_SHA256(key="WebAppData", msg=BOT_TOKEN)`, `hash = hex(HMAC_SHA256(secret, строка))`.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::maxapi {

/// Максимальный возраст initData (АРХ §8).
inline constexpr std::chrono::hours kInitDataMaxAge{24};

/// Проверенные данные запуска мини-приложения.
struct InitData {
  std::int64_t user_id{0};
  std::string first_name{};
  std::int64_t auth_date{0};  ///< Unix-время в секундах.
  std::optional<std::string> start_param{};
};

/// Проверяет подпись и срок initData.
/// Ошибки: `kUnauthorized` — нет, испорчена или подделана; `kInitDataExpired` — старше `max_age`.
/// @param raw строка `window.WebApp.initData` как есть (из заголовка `X-Max-Init-Data`).
/// @param now текущее время (инъекция для тестов).
[[nodiscard]] Result<InitData> validate_init_data(std::string_view raw, std::string_view bot_token,
                                                  std::chrono::system_clock::time_point now,
                                                  std::chrono::seconds max_age = kInitDataMaxAge);

/// Сравнение секрета webhook `X-Max-Bot-Api-Secret` за константное время (`CRYPTO_memcmp`).
/// Пустой ожидаемый секрет не совпадает ни с чем.
[[nodiscard]] bool secret_matches(std::string_view received, std::string_view expected) noexcept;

/// Процентное декодирование (`%XX`); `+` остаётся `+`, как в `decodeURIComponent`.
/// @return `nullopt` при некорректной последовательности.
[[nodiscard]] std::optional<std::string> percent_decode(std::string_view s);

/// Процентное кодирование для query-параметров (RFC 3986 unreserved остаются как есть).
[[nodiscard]] std::string percent_encode(std::string_view s);

}  // namespace sk::maxapi
