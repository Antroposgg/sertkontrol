#include "sertkontrol/verify/inn.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace sk::verify {

namespace {

template <std::size_t N>
int check_digit(std::string_view digits, const std::array<int, N>& weights) {
  int sum = 0;
  for (std::size_t i = 0; i < N; ++i) {
    sum += weights.at(i) * (digits[i] - '0');
  }
  return sum % 11 % 10;
}

}  // namespace

bool inn_valid(std::string_view inn) noexcept {
  if ((inn.size() != 10 && inn.size() != 12) ||
      !std::ranges::all_of(inn, [](char c) { return c >= '0' && c <= '9'; })) {
    return false;
  }
  if (inn.size() == 10) {
    constexpr std::array<int, 9> kW{2, 4, 10, 3, 5, 9, 4, 6, 8};
    return check_digit(inn, kW) == inn[9] - '0';
  }
  constexpr std::array<int, 10> kW11{7, 2, 4, 10, 3, 5, 9, 4, 6, 8};
  constexpr std::array<int, 11> kW12{3, 7, 2, 4, 10, 3, 5, 9, 4, 6, 8};
  return check_digit(inn, kW11) == inn[10] - '0' && check_digit(inn, kW12) == inn[11] - '0';
}

}  // namespace sk::verify
