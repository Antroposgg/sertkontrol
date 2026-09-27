/// @file config.hpp
/// @brief Конфигурация `certd` из переменных окружения (список — в .env.example и README).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Параметры запуска `certd`.
struct Config {
  std::uint16_t port{8080};
  std::size_t threads{0};     ///< Потоки IO Drogon; 0 — по числу ядер.
  std::string pg_conninfo{};  ///< Строка подключения libpq `key='value' …`.
  std::size_t db_connections{4};
  std::filesystem::path snapshot_dir{"/data/snapshots"};
  std::filesystem::path web_root{"/srv/app"};  ///< Статика мини-приложения (`web/dist`).

  // ── MAX ──
  std::string max_bot_token{};  ///< Пусто — бот и проверка initData выключены.
  std::string max_webhook_secret{};  ///< Обязателен, если задан токен: `[A-Za-z0-9_-]{5,256}` (dev.max.ru).
  std::string max_bot_username{};  ///< Публичное имя бота — для кнопок `open_app`.
  std::string max_api_base_url{"https://platform-api2.max.ru"};

  /// Пользователь мини-приложения без MAX (локальный запуск). Только при пустом токене — ADR-0013.
  std::optional<std::int64_t> dev_user_id{};
  bool dev_user_id_ignored{false};  ///< CERTD_DEV_USER_ID задан, но проигнорирован: есть токен бота.

  // ── Распознавание ──
  std::size_t recog_threads{2};
  std::size_t recog_queue{8};  ///< Одновременных задач сверх — «попробуйте через минуту».

  /// Бот включён (есть токен и секрет).
  [[nodiscard]] bool bot_enabled() const noexcept { return !max_bot_token.empty(); }
};

/// Источник переменных окружения; инъекция для тестов.
using EnvLookup = std::function<std::optional<std::string>(std::string_view name)>;

/// Чтение настоящего окружения процесса через `std::getenv`.
[[nodiscard]] std::optional<std::string> process_env(std::string_view name);

/// Собирает `Config`. Ошибка `kInvalidArgument` — если число вне диапазона или не число.
[[nodiscard]] Result<Config> load_config(const EnvLookup& env);

/// Экранирует значение для строки подключения libpq: `'…'`, `\` и `'` через `\`.
[[nodiscard]] std::string conninfo_quote(std::string_view value);

}  // namespace sk::certd
