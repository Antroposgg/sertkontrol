#include "media.hpp"

#include <algorithm>
#include <array>

namespace sk::certd {

std::optional<recog::MediaType> detect_media_type(std::span<const std::byte> bytes) {
  const auto starts_with = [&](std::initializer_list<unsigned char> sig) {
    return bytes.size() >= sig.size() &&
           std::equal(sig.begin(), sig.end(), bytes.begin(),
                      [](unsigned char a, std::byte b) { return static_cast<std::byte>(a) == b; });
  };
  if (starts_with({0xFF, 0xD8, 0xFF})) {
    return recog::MediaType::kJpeg;
  }
  if (starts_with({0x89, 'P', 'N', 'G'})) {
    return recog::MediaType::kPng;
  }
  constexpr std::array<std::byte, 5> kPdf{std::byte{'%'}, std::byte{'P'}, std::byte{'D'}, std::byte{'F'},
                                          std::byte{'-'}};
  const auto head = bytes.first(std::min<std::size_t>(bytes.size(), 1024));
  if (!std::ranges::search(head, kPdf).empty()) {
    return recog::MediaType::kPdf;
  }
  return std::nullopt;
}

}  // namespace sk::certd
