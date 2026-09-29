/// Разбор тела webhook MAX (`Update`) на произвольном JSON.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sertkontrol/maxapi/update.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const auto u = sk::maxapi::parse_update({reinterpret_cast<const char*>(data), size});
  if (u) {
    (void)sk::maxapi::dedup_key(u.value());
  }
  return 0;
}
