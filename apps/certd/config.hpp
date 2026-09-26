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
