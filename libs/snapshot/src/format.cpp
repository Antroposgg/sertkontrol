#include "sertkontrol/snapshot/format.hpp"

namespace sk::snapshot::format {

bool serial_key(std::string_view serial, std::uint8_t year, std::uint64_t& key) noexcept {
  if (serial.empty() || serial.size() > 10 || year > 99) {
    return false;
  }
  std::uint64_t value = 0;
  for (const char c : serial) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  key = value * 100 + year;
  return true;
}

}  // namespace sk::snapshot::format
