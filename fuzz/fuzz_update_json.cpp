/// Разбор JSON от MAX на произвольном входе: тело webhook (`Update`) и ответ `GET /me` (`BotInfo`).
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sertkontrol/maxapi/bot_api.hpp"
#include "sertkontrol/maxapi/update.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  const auto u = sk::maxapi::parse_update(text);
  if (u) {
    (void)sk::maxapi::dedup_key(u.value());
  }
  (void)sk::maxapi::parse_bot_info(text);
  return 0;
}
