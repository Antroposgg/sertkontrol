/// @file health.hpp
/// @brief Логика `GET /healthz` без зависимости от HTTP-фреймворка.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sk::certd {

/// Что известно о состоянии процесса на момент проверки.
struct HealthInputs {
  bool db_ok{false};                                ///< `SELECT 1` в PostgreSQL прошёл.
  std::optional<std::uint64_t> snapshot_version{};  ///< Версия загруженного снапшота, если есть.
  /// Требовать ли снапшот для готовности. Этап 0: false (снапшотов ещё нет);
  /// с этапа 1 — true, как в АРХ §3 («снапшот загружен, БД доступна»).
  bool snapshot_required{false};
};

/// Ответ `/healthz`: HTTP-статус и JSON-тело.
struct HealthReport {
  int http_status{503};
  std::string body{};
};

/// 200 — если БД доступна и (снапшот загружен или не требуется), иначе 503.
[[nodiscard]] HealthReport evaluate_health(const HealthInputs& in);

}  // namespace sk::certd
