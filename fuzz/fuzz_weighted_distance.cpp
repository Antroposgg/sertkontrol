/// Взвешенное расстояние Левенштейна: симметрия, ноль на равных строках, не больше длины длинной строки.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "sertkontrol/verify/fuzzy.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const std::string_view in{reinterpret_cast<const char*>(data), size};
  const auto split = in.find('\n');
  const auto a = in.substr(0, split);
  const auto b = split == std::string_view::npos ? std::string_view{} : in.substr(split + 1);
  const auto ab = sk::verify::weighted_distance(a, b);
  const auto ba = sk::verify::weighted_distance(b, a);
  if (std::abs(ab - ba) > 1e-9 || ab < 0 || sk::verify::weighted_distance(a, a) != 0.0 ||
      ab > static_cast<double>(std::max(a.size(), b.size()))) {
    std::abort();
  }
  return 0;
}
