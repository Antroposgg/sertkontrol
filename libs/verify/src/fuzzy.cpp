#include "sertkontrol/verify/fuzzy.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace sk::verify {

namespace {

constexpr std::size_t kMaxCodePoints = 64;

/// Кодовые точки UTF-8; некорректный байт — отдельная «точка», чтобы разбор не падал на мусоре.
std::vector<char32_t> code_points(std::string_view s) {
  std::vector<char32_t> out;
  out.reserve(std::min(s.size(), kMaxCodePoints));
  for (std::size_t i = 0; i < s.size() && out.size() < kMaxCodePoints;) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    char32_t cp = c;
    if ((c & 0xE0U) == 0xC0U) {
      len = 2;
      cp = c & 0x1FU;
    } else if ((c & 0xF0U) == 0xE0U) {
      len = 3;
      cp = c & 0x0FU;
    } else if ((c & 0xF8U) == 0xF0U) {
      len = 4;
      cp = c & 0x07U;
    }
    if (len > 1 && i + len <= s.size()) {
      for (std::size_t k = 1; k < len; ++k) {
        cp = (cp << 6U) | (static_cast<unsigned char>(s[i + k]) & 0x3FU);
      }
    } else if (c >= 0x80U) {
      len = 1;
      cp = 0xDC00U + c;  // одиночный байт вне UTF-8 — уникальная точка
    }
    out.push_back(cp);
    i += len;
  }
  return out;
}

/// Буква, которую OCR путает с цифрой, → цифра; иначе 0.
char32_t digit_twin(char32_t c) {
  switch (c) {
    case U'O':
      return U'0';
    case U'B':
      return U'8';
    case U'S':
      return U'5';
    case U'I':
      return U'1';
    case U'Z':
      return U'2';
    default:
      return 0;
  }
}

double substitution_cost(char32_t a, char32_t b) {
  if (a == b) {
    return 0.0;
  }
  if (digit_twin(a) == b || digit_twin(b) == a) {
    return kOcrSubstitutionCost;
  }
  return 1.0;
}

/// Серия из хвоста `…<серия>/<ГГ>`: буквы-двойники заменены цифрами, одна непонятная позиция (буква без
/// двойника) помечена `?`. Год — две цифры после замены двойников.
struct FoldedSerial {
  std::string serial{};
  std::uint8_t year{0};
  std::optional<std::size_t> unknown{};  ///< Позиция `?`, если есть.
};

char fold_digit(char c) {
  const auto twin = digit_twin(static_cast<unsigned char>(c));
  return twin != 0 ? static_cast<char>(twin) : c;
}

std::optional<FoldedSerial> folded_serial(std::string_view canonical) {
  const auto slash = canonical.rfind('/');
  if (slash == std::string_view::npos || canonical.size() - slash != 3) {
    return std::nullopt;
  }
  const auto dot = canonical.rfind('.', slash);
  if (dot == std::string_view::npos || slash - dot < 2 || slash - dot > 11) {
    return std::nullopt;
  }
  FoldedSerial out;
  for (const char c : canonical.substr(dot + 1, slash - dot - 1)) {
    const char d = fold_digit(c);
    if (d >= '0' && d <= '9') {
      out.serial.push_back(d);
      continue;
    }
    if (out.unknown) {
      return std::nullopt;  // две непонятные позиции — это уже не «одна цифра»
    }
    out.unknown = out.serial.size();
    out.serial.push_back('?');
  }
  const char y1 = fold_digit(canonical[slash + 1]);
  const char y2 = fold_digit(canonical[slash + 2]);
  if (y1 < '0' || y1 > '9' || y2 < '0' || y2 > '9') {
    return std::nullopt;
  }
  out.year = static_cast<std::uint8_t>(((y1 - '0') * 10) + (y2 - '0'));
  return out;
}

}  // namespace

double weighted_distance(std::string_view a, std::string_view b) {
  const auto x = code_points(a);
  const auto y = code_points(b);
  std::vector<double> prev(y.size() + 1);
  std::vector<double> cur(y.size() + 1);
  for (std::size_t j = 0; j <= y.size(); ++j) {
    prev[j] = static_cast<double>(j);
  }
  for (std::size_t i = 1; i <= x.size(); ++i) {
    cur[0] = static_cast<double>(i);
    for (std::size_t j = 1; j <= y.size(); ++j) {
      cur[j] =
          std::min({prev[j] + 1.0, cur[j - 1] + 1.0, prev[j - 1] + substitution_cost(x[i - 1], y[j - 1])});
    }
    std::swap(prev, cur);
  }
  return prev[y.size()];
}

FuzzyMatch fuzzy_match(const snapshot::Snapshot& snap, std::string_view canonical) {
  FuzzyMatch out;
  const auto serial = folded_serial(canonical);
  if (!serial) {
    return out;
  }
  const auto& base = serial->serial;
  const auto year = serial->year;
  std::set<std::uint32_t> indices;
  const auto collect = [&](const std::string& s) {
    for (const auto idx : snap.by_serial(s, year)) {
      indices.insert(idx);
    }
  };
  std::string variant = base;
  if (serial->unknown) {
    // Непонятная позиция — это и есть искажённая цифра: 10 вариантов вместо длина × 9.
    for (char d = '0'; d <= '9'; ++d) {
      variant[*serial->unknown] = d;
      collect(variant);
    }
  } else {
    collect(base);
    // Серия искажена одной цифрой: длина × 9 запросов к индексу — дешевле BK-дерева по всем ключам (АРХ
    // §7.2).
    for (std::size_t i = 0; i < base.size(); ++i) {
      for (char d = '0'; d <= '9'; ++d) {
        if (d == base[i]) {
          continue;
        }
        variant[i] = d;
        collect(variant);
      }
      variant[i] = base[i];
    }
  }
  for (const auto idx : indices) {
    const auto rec = snap.record(idx);
    if (rec.number == canonical) {
      continue;
    }
    out.ranked.push_back(
        {.number = std::string{rec.number}, .distance = weighted_distance(canonical, rec.number)});
  }
  std::ranges::sort(out.ranked, [](const Suggestion& l, const Suggestion& r) {
    return std::pair{l.distance, std::string_view{l.number}} <
           std::pair{r.distance, std::string_view{r.number}};
  });
  if (!out.ranked.empty()) {
    const auto best = out.ranked.front().distance;
    const bool alone = out.ranked.size() == 1 || out.ranked[1].distance - best >= kMinGap - 1e-9;
    out.confident = best <= kAcceptDistance + 1e-9 && alone;
  }
  return out;
}

}  // namespace sk::verify
