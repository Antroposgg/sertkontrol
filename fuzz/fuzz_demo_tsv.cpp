/// Парсер источника (C3): TSV-адаптер демо-данных и нормализация записей на произвольном файле.
#include <cstddef>
#include <cstdint>

#include "demo_source.hpp"
#include "fuzz_util.hpp"
#include "normalize.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const sk::fuzz::MemFile file{data, size};
  auto src = sk::ingest::DemoTsvSource::open(file.path());
  if (src) {
    (void)sk::ingest::normalize_all(src.value());
  }
  return 0;
}
