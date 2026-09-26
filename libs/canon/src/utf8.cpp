#include "utf8.hpp"

namespace sk::canon::detail {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

bool is_continuation(unsigned char c) {
  return (c & 0xC0U) == 0x80U;
}

}  // namespace

std::vector<CodePoint> decode(std::string_view s) {
  std::vector<CodePoint> out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c0 = static_cast<unsigned char>(s[i]);
    std::size_t len = 0;
    char32_t cp = 0;
    char32_t min = 0;
    if (c0 < 0x80U) {
      len = 1;
      cp = c0;
    } else if ((c0 & 0xE0U) == 0xC0U) {
      len = 2;
      cp = c0 & 0x1FU;
      min = 0x80;
    } else if ((c0 & 0xF0U) == 0xE0U) {
      len = 3;
      cp = c0 & 0x0FU;
      min = 0x800;
    } else if ((c0 & 0xF8U) == 0xF0U) {
      len = 4;
      cp = c0 & 0x07U;
      min = 0x10000;
    }
    bool ok = len != 0 && i + len <= s.size();
    for (std::size_t k = 1; ok && k < len; ++k) {
      const auto ck = static_cast<unsigned char>(s[i + k]);
      ok = is_continuation(ck);
      cp = (cp << 6U) | (ck & 0x3FU);
    }
    // Отвергаем overlong-кодирование, суррогаты и значения за пределами Unicode.
    ok = ok && cp >= min && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF);
    if (!ok) {
      out.push_back({kReplacement, i, 1});
      ++i;
      continue;
    }
    out.push_back({cp, i, len});
    i += len;
  }
  return out;
}

void append(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

}  // namespace sk::canon::detail
