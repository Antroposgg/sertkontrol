/// @file registry_link.hpp
/// @brief Номер документа по ссылке на запись реестра (QR выписки, F10) — общий для домена и его фейка.
#pragma once

#include <string>
#include <string_view>

#include "sertkontrol/verify/text.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Каноничный номер записи, на которую ведёт ссылка из `text` (`verify::parse_registry_url`).
/// Ошибки: `kNumberNotRecognized` — в тексте нет ни номера, ни ссылки на реестр; `kNotFoundInSnapshot` —
/// записи с таким ID и видом документа нет в данных (сообщение называет дату данных).
[[nodiscard]] Result<std::string> number_by_registry_link(const snapshot::Snapshot& snap,
                                                          std::string_view text);

}  // namespace sk::certd
