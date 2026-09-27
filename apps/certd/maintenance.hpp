/// @file maintenance.hpp
/// @brief Фоновые задачи `certd`: имена задач `job`, их расписание и чистка по срокам хранения (АРХ §6).
#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// `notify_changes(v)` для боевой версии; payload `{"version": v}` (АРХ §4, поток B, шаг 4).
inline constexpr std::string_view kJobNotifyChanges = "notify_changes";
/// Ежедневная чистка по срокам хранения; payload `{}`.
inline constexpr std::string_view kJobCleanup = "cleanup";
/// Второй прогон `notify_changes` подбирает строки, вставленные в портфель во время первого (ТЗ R3.9).
inline constexpr std::chrono::seconds kNotifySecondPass{600};

/// Ключ дедупликации прогона `pass` (1 или 2) задачи уведомлений версии `version`.
[[nodiscard]] inline std::string notify_dedup_key(std::uint64_t version, int pass) {
  return std::to_string(version) + ":" + std::to_string(pass);
}

/// Число удалённых строк по таблицам.
struct CleanupStats {
  std::size_t check_log{0};
  std::size_t inbound_update{0};
  std::size_t outbox{0};
  std::size_t dialog_state{0};
};

/// Сроки хранения (АРХ §6): `check_log` — 90 дней, `inbound_update` — 7 дней, отправленный и отвергнутый
/// `outbox` — 30 дней, `dialog_state` старше суток (TTL диалога — 30 минут).
drogon::Task<Result<CleanupStats>> cleanup_retention(drogon::orm::DbClientPtr db);

}  // namespace sk::certd
