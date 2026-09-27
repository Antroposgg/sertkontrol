/// @file lookup.hpp
/// @brief Точный поиск записи снапшота по канонической строке (АРХ §7.2) — для потребителей вне
/// `libs/verify`.
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::snapshot {

/// Индекс записи с номером `canonical` или `nullopt`. XXH3 → `equal_range` по отсортированным ключам →
/// сравнение полной строки: коллизии 64-битного хэша ненулевые, поэтому проверяется весь диапазон.
[[nodiscard]] inline std::optional<std::size_t> find_index(const Snapshot& snap, std::string_view canonical) {
  const auto keys = snap.keys();
  const auto [lo, hi] = std::equal_range(keys.begin(), keys.end(), canon::key_hash(canonical));
  for (auto it = lo; it != hi; ++it) {
    const auto index = static_cast<std::size_t>(it - keys.begin());
    if (snap.record(index).number == canonical) {
      return index;
    }
  }
  return std::nullopt;
}

}  // namespace sk::snapshot
