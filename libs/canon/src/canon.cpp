/// @file canon.cpp
/// @brief C1: канонизация и грамматика номера (АРХ §7.1). Правила — в libs/canon/README.md.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <xxhash.h>

#include "sertkontrol_contracts.hpp"
#include "utf8.hpp"

namespace sk::canon {

namespace {

using detail::CodePoint;

constexpr char32_t kSpace = U' ';

bool is_space(char32_t cp) {
  return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == U'\f' || cp == U'\v' ||
         cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

bool is_dash(char32_t cp) {
  return cp == U'-' || (cp >= 0x2010 && cp <= 0x2015) || cp == 0x2212;
}

/// Кириллица, совпадающая по начертанию с латиницей, и Д → D (АРХ §7.1, шаг 2).
char32_t homoglyph(char32_t upper) {
  switch (upper) {
    case U'А':
      return U'A';
    case U'В':
      return U'B';
    case U'Е':
      return U'E';
    case U'К':
      return U'K';
    case U'М':
      return U'M';
    case U'Н':
      return U'H';
    case U'О':
      return U'O';
    case U'Р':
      return U'P';
    case U'С':
      return U'C';
    case U'Т':
      return U'T';
    case U'У':
      return U'Y';
    case U'Х':
      return U'X';
    case U'Д':
      return U'D';
    default:
      return upper;
  }
}

/// Шаги 1–2 АРХ §7.1 для одного символа: верхний регистр, гомоглифы; все пробелы → ' ', все тире → '-'.
/// Цифры не трогаются: 0/O различает нечёткий поиск.
char32_t normalize(char32_t cp) {
  if (is_space(cp)) {
    return kSpace;
  }
  if (is_dash(cp)) {
    return U'-';
  }
  if (cp >= U'a' && cp <= U'z') {
    return cp - (U'a' - U'A');
  }
  if (cp >= 0x0430 && cp <= 0x044F) {  // а–я → А–Я
    cp -= 0x20;
  } else if (cp == 0x0451) {  // ё → Ё
    cp = 0x0401;
  }
  return homoglyph(cp);
}

bool is_ascii_alnum(char32_t cp) {
  return (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z') || (cp >= U'0' && cp <= U'9');
}

bool is_trailing_punct(char32_t cp) {
  return cp == U'.' || cp == U',' || cp == U';' || cp == U':' || cp == U')' || cp == U']' || cp == U'}' ||
         cp == U'"' || cp == U'\'' || cp == U'»' || cp == U'”' || cp == U'!' || cp == U'?';
}

/// Совпадение префикса «RU [D|C] [-]» в позиции `i` нормализованной последовательности.
struct Prefix {
  DocKind kind{DocKind::kDeclaration};
  std::size_t body_begin{0};  ///< Индекс первого символа после префикса и пробелов.
};

std::optional<Prefix> match_prefix(const std::vector<CodePoint>& orig, const std::vector<char32_t>& norm,
                                   std::size_t i) {
  if (i + 2 >= norm.size() || norm[i] != U'R' || norm[i + 1] != U'U') {
    return std::nullopt;
  }
  // «RU» должно начинать слово: слева не латинская буква или цифра исходного текста
  // (кириллическое «ЕАЭС» слитно с «RU» допустимо).
  if (i > 0 && is_ascii_alnum(orig[i - 1].cp)) {
    return std::nullopt;
  }
  std::size_t j = i + 2;
  while (j < norm.size() && norm[j] == kSpace) {
    ++j;
  }
  if (j >= norm.size() || (norm[j] != U'D' && norm[j] != U'C')) {
    return std::nullopt;
  }
  Prefix p{.kind = norm[j] == U'C' ? DocKind::kCertificate : DocKind::kDeclaration};
  ++j;
  while (j < norm.size() && (norm[j] == kSpace || norm[j] == U'-')) {
    ++j;
  }
  if (j >= norm.size()) {
    return std::nullopt;
  }
  p.body_begin = j;
  return p;
}

std::string build_canonical(DocKind kind, const std::vector<char32_t>& norm, std::size_t begin,
                            std::size_t end) {
  // Хвостовая пунктуация («…/26.» в конце предложения) не входит в номер.
  while (end > begin && (norm[end - 1] == kSpace || is_trailing_punct(norm[end - 1]))) {
    --end;
  }
  std::string out = kind == DocKind::kCertificate ? "RUC-" : "RUD-";
  bool any = false;
  for (std::size_t k = begin; k < end; ++k) {
    if (norm[k] != kSpace) {
      detail::append(out, norm[k]);
      any = true;
    }
  }
  return any ? out : std::string{};
}

bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

}  // namespace

std::optional<std::string> canonicalize(std::string_view raw) {
  const auto orig = detail::decode(raw);
  std::vector<char32_t> norm;
  norm.reserve(orig.size());
  for (const auto& c : orig) {
    norm.push_back(normalize(c.cp));
  }
  for (std::size_t i = 0; i < norm.size(); ++i) {
    if (const auto p = match_prefix(orig, norm, i)) {
      auto out = build_canonical(p->kind, norm, p->body_begin, norm.size());
      if (out.empty()) {
        return std::nullopt;
      }
      return out;
    }
  }
  return std::nullopt;
}

std::optional<Number> parse(std::string_view canonical) {
  if (canonical.size() < 5 || !(canonical.starts_with("RUD-") || canonical.starts_with("RUC-"))) {
    return std::nullopt;
  }
  const auto body = canonical.substr(4);
  // Допустимые символы тела: латиница A–Z, цифры, «.», «/», «-» и кириллица верхнего регистра
  // (буквы без латинского двойника, например «Я» в «АЯ46»).
  for (const auto& c : detail::decode(body)) {
    const bool ok = (c.cp >= U'A' && c.cp <= U'Z') || (c.cp >= U'0' && c.cp <= U'9') || c.cp == U'.' ||
                    c.cp == U'/' || c.cp == U'-' || (c.cp >= 0x0410 && c.cp <= 0x042F) || c.cp == 0x0401;
    if (!ok) {
      return std::nullopt;
    }
  }
  // Формат ЕАЭС: …<сегменты через точку>.<серия>/<ГГ>.
  const auto slash = body.rfind('/');
  if (slash == std::string_view::npos || body.size() - slash != 3) {
    return std::nullopt;
  }
  const auto dot = body.rfind('.', slash);
  if (dot == std::string_view::npos || dot == 0) {
    return std::nullopt;
  }
  const auto serial = body.substr(dot + 1, slash - dot - 1);
  const auto yy = body.substr(slash + 1);
  if (serial.empty() || serial.size() > 10 || !std::ranges::all_of(serial, is_digit) ||
      !std::ranges::all_of(yy, is_digit)) {
    return std::nullopt;
  }
  Number n;
  n.canonical = std::string{canonical};
  n.kind = canonical[2] == 'C' ? DocKind::kCertificate : DocKind::kDeclaration;
  n.serial = std::string{serial};
  n.year = static_cast<std::uint8_t>((yy[0] - '0') * 10 + (yy[1] - '0'));
  return n;
}

std::uint64_t key_hash(std::string_view canonical) noexcept {
  return XXH3_64bits(canonical.data(), canonical.size());
}

std::vector<std::string> find_numbers(std::string_view text, std::size_t max_count) {
  const auto orig = detail::decode(text);
  std::vector<char32_t> norm;
  norm.reserve(orig.size());
  for (const auto& c : orig) {
    norm.push_back(normalize(c.cp));
  }
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  std::size_t i = 0;
  while (i < norm.size() && out.size() < max_count) {
    const auto p = match_prefix(orig, norm, i);
    if (!p) {
      ++i;
      continue;
    }
    // Тело номера — до пробела или разделителя списка.
    std::size_t end = p->body_begin;
    while (end < norm.size() && norm[end] != kSpace && norm[end] != U',' && norm[end] != U';') {
      ++end;
    }
    const auto canonical = build_canonical(p->kind, norm, p->body_begin, end);
    if (!canonical.empty() && seen.insert(canonical).second) {
      const auto first = orig[i].offset;
      const auto last = orig[end - 1].offset + orig[end - 1].length;
      std::string raw{text.substr(first, last - first)};
      // Хвостовую пунктуацию из сырой строки тоже убираем, чтобы запрос выглядел как номер.
      while (!raw.empty() && std::string_view{".,;:)]}\"'!?"}.find(raw.back()) != std::string_view::npos) {
        raw.pop_back();
      }
      out.push_back(std::move(raw));
    }
    i = end;
  }
  return out;
}

}  // namespace sk::canon
