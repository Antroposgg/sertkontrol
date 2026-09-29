/// Грамматика и канонизация номера на произвольном тексте: без падений, идемпотентность, согласие с parse.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "sertkontrol_contracts.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  if (const auto c = sk::canon::canonicalize(text)) {
    if (sk::canon::canonicalize(*c) != c) {
      std::abort();  // канонизация обязана быть идемпотентной (golden-файл C1)
    }
    if (const auto n = sk::canon::parse(*c); n && n->canonical != *c) {
      std::abort();
    }
    (void)sk::canon::key_hash(*c);
  }
  for (const auto& raw : sk::canon::find_numbers(text, 20)) {
    if (!sk::canon::canonicalize(raw)) {
      std::abort();  // find_numbers возвращает только то, что канонизируется
    }
  }
  return 0;
}
