/// Проверка initData на произвольной строке: только отказ или подпись, без падений.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sertkontrol/maxapi/auth.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const std::string_view raw{reinterpret_cast<const char*>(data), size};
  (void)sk::maxapi::validate_init_data(raw, "fuzz-token", std::chrono::system_clock::time_point{});
  (void)sk::maxapi::percent_decode(raw);
  return 0;
}
