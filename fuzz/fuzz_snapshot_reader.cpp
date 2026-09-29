/// Ридер снапшота на битых файлах (АРХ §10). Контрольная сумма пересчитывается, чтобы фаззер доходил до
/// структурных проверок, а не отсекался на XXH3.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <xxhash.h>

#include "fuzz_util.hpp"
#include "sertkontrol/snapshot/format.hpp"
#include "sertkontrol_contracts.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  namespace fmt = sk::snapshot::format;
  std::vector<std::uint8_t> bytes(data,
                                  data + size);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  if (bytes.size() >= sizeof(fmt::FileHeader)) {
    fmt::FileHeader h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    h.payload_xxh3 = XXH3_64bits(bytes.data() + sizeof(h), bytes.size() - sizeof(h));
    std::memcpy(bytes.data(), &h, sizeof(h));
  }
  const sk::fuzz::MemFile file{bytes.data(), bytes.size()};
  const auto snap = sk::snapshot::open_snapshot(file.path());
  if (!snap) {
    return 0;
  }
  // Открытый снапшот обязан читаться целиком без выхода за границы.
  const auto& s = *snap.value();
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto r = s.record(i);
    (void)s.by_serial(r.number.substr(0, 5), 26);
  }
  return 0;
}
