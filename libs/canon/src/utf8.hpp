/// @file utf8.hpp
/// @brief Минимальный декодер/кодер UTF-8 для канонизации (внутренний заголовок libs/canon).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sk::canon::detail {

/// Кодовая точка и её положение в исходной строке.
struct CodePoint {
  char32_t cp{0};
  std::size_t offset{0};  ///< Смещение первого байта в исходной строке.
  std::size_t length{0};  ///< Длина в байтах (1–4).
};

/// Декодирует UTF-8. Некорректная последовательность даёт U+FFFD длиной 1 байт — разбор не падает
/// и не зацикливается на любом входе.
[[nodiscard]] std::vector<CodePoint> decode(std::string_view s);

/// Дописывает кодовую точку в UTF-8.
void append(std::string& out, char32_t cp);

}  // namespace sk::canon::detail
