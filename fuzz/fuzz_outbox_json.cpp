/// Строка outbox (C9) → проверка лимитов MAX → тело `POST /messages`.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sertkontrol/maxapi/message.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const auto m = sk::maxapi::from_outbox_json({reinterpret_cast<const char*>(data), size});
  if (m) {
    (void)sk::maxapi::validate(m.value());
    (void)sk::maxapi::to_new_message_body(m.value(), "bot");
    (void)sk::maxapi::to_outbox_json(m.value());
  }
  return 0;
}
