/// @file inn.hpp
/// @brief Проверка ИНН по контрольным цифрам (АРХ §7.7).
#pragma once

#include <string_view>

namespace sk::verify {

/// `true`, если строка — 10- или 12-значный ИНН с верными контрольными цифрами.
/// Формулы: d10 = (Σ wᵢdᵢ mod 11) mod 10, w = (2,4,10,3,5,9,4,6,8); для 12 цифр — две контрольные
/// с весами (7,2,4,10,3,5,9,4,6,8) и (3,7,2,4,10,3,5,9,4,6,8).
[[nodiscard]] bool inn_valid(std::string_view inn) noexcept;

}  // namespace sk::verify
