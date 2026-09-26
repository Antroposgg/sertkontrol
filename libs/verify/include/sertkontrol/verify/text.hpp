/// @file text.hpp
/// @brief Общие для бота и REST представления вердикта: даты, ссылки на реестр, названия статусов.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::verify {

/// Горизонт «срок скоро истекает» в днях (правило `term.expiring`, docs/rules.md).
inline constexpr int kExpiringSoonDays = 30;
/// Сколько ближайших номеров показывать, если точного совпадения нет.
inline constexpr std::size_t kMaxSuggestions = 3;

/// `ДД.ММ.ГГГГ`.
[[nodiscard]] std::string format_date(Date date);

/// Название статуса по-русски: «действует», «приостановлен», …
[[nodiscard]] std::string_view status_name(snapshot::Status status) noexcept;

/// «Декларация» или «Сертификат».
[[nodiscard]] std::string_view kind_name(canon::DocKind kind) noexcept;

/// Ссылка на запись в открытом реестре Росаккредитации; при `registry_id == 0` — страница поиска реестра.
[[nodiscard]] std::string registry_url(canon::DocKind kind, std::uint64_t registry_id);

/// Номер в привычном виде для людей: `RUD-…` → `RU Д-…`, `RUC-…` → `RU С-…`.
[[nodiscard]] std::string display_number(std::string_view canonical);

}  // namespace sk::verify
