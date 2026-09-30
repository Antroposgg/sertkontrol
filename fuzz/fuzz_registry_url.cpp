/// Ссылка на запись реестра из QR выписки (F10): без падений; найденная ссылка восстанавливается и
/// разбирается в тот же ID и вид документа.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "sertkontrol/verify/text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  const auto ref = sk::verify::parse_registry_url(text);
  if (!ref.has_value()) {
    return 0;
  }
  if (ref->registry_id == 0) {
    std::abort();  // 0 — «ID неизвестен», ключом поиска не бывает
  }
  const auto back = sk::verify::parse_registry_url(sk::verify::registry_url(ref->kind, ref->registry_id));
  if (!back.has_value() || back->registry_id != ref->registry_id || back->kind != ref->kind) {
    std::abort();
  }
  return 0;
}
