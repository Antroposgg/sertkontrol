/// @file checked.hpp
/// @brief Проверенный доступ к `std::optional` в тестах: пустое значение — исключение, а не UB.
#pragma once

#include <optional>
#include <stdexcept>

namespace sk::test {

/// Возвращает `*o` или бросает `std::logic_error`, если `o` пуст. Тест при этом падает с сообщением.
template <class T>
const T& checked(const std::optional<T>& o) {
  if (!o.has_value()) {
    throw std::logic_error("ожидалось непустое optional");
  }
  return *o;
}

}  // namespace sk::test
