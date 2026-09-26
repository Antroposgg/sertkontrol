/// @file media.hpp
/// @brief Тип загруженного файла по сигнатуре (имени и Content-Type не доверяем).
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// PDF (`%PDF-` в первых 1024 байтах), JPEG (`FF D8 FF`), PNG (`89 50 4E 47`); иначе `nullopt` → 415.
[[nodiscard]] std::optional<recog::MediaType> detect_media_type(std::span<const std::byte> bytes);

}  // namespace sk::certd
