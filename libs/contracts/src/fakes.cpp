#include "sertkontrol/fakes.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace sk::fake {

namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date ymd(int y, unsigned m, unsigned d) {
  return Date{year{y} / month{m} / day{d}};
}

bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

std::string trim(std::string_view s) {
  const auto* first = std::find_if_not(
      s.begin(), s.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
  const auto* last = std::find_if_not(s.rbegin(), std::make_reverse_iterator(first), [](char c) {
                       return std::isspace(static_cast<unsigned char>(c)) != 0;
                     }).base();
  return {first, last};
}

}  // namespace

std::optional<std::string> canonicalize(std::string_view raw) {
  std::string compact;
  compact.reserve(raw.size());
  for (const char c : raw) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      continue;
    }
    compact.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  for (const std::string_view prefix : {std::string_view{"RUD-"}, std::string_view{"RUC-"}}) {
    if (auto pos = compact.find(prefix); pos != std::string::npos) {
      return compact.substr(pos);
    }
  }
  return std::nullopt;
}

std::optional<canon::Number> parse(std::string_view canonical) {
  if (canonical.size() < 4 || !canonical.starts_with("RU") || canonical[3] != '-') {
    return std::nullopt;
  }
  const auto slash = canonical.rfind('/');
  const auto dot = canonical.rfind('.', slash);
  if (slash == std::string_view::npos || dot == std::string_view::npos || canonical.size() != slash + 3) {
    return std::nullopt;
  }
  const auto serial = canonical.substr(dot + 1, slash - dot - 1);
  const auto yy = canonical.substr(slash + 1);
  if (serial.empty() || !std::ranges::all_of(serial, is_digit) || !std::ranges::all_of(yy, is_digit)) {
    return std::nullopt;
  }
  canon::Number n;
  n.canonical = std::string{canonical};
  n.kind = canonical[2] == 'C' ? canon::DocKind::kCertificate : canon::DocKind::kDeclaration;
  n.serial = std::string{serial};
  n.year = static_cast<std::uint8_t>((yy[0] - '0') * 10 + (yy[1] - '0'));
  return n;
}

std::uint64_t key_hash(std::string_view canonical) noexcept {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t h = kOffset;
  for (const char c : canonical) {
    h ^= static_cast<unsigned char>(c);
    h *= kPrime;
  }
  return h;
}

std::vector<std::string> find_numbers(std::string_view text, std::size_t max_count) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= text.size() && out.size() < max_count) {
    const auto end = text.find_first_of("\n,;", start);
    const auto piece =
        trim(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
    if (piece.find("RU") != std::string::npos || piece.find("ru") != std::string::npos) {
      out.push_back(piece);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return out;
}

namespace {

/// Полный порядок снапшота: (хэш, каноническая строка) — АРХ §7.3.
std::vector<FakeRecord> sorted_by_key(std::vector<FakeRecord> records) {
  std::ranges::sort(records, [](const FakeRecord& a, const FakeRecord& b) {
    return std::pair{key_hash(a.number), a.number} < std::pair{key_hash(b.number), b.number};
  });
  return records;
}

}  // namespace

FakeSnapshot::FakeSnapshot(std::vector<FakeRecord> records, snapshot::SnapshotMeta meta)
    : records_(sorted_by_key(std::move(records))), meta_(std::move(meta)) {
  keys_.reserve(records_.size());
  for (const auto& r : records_) {
    keys_.push_back(key_hash(r.number));
  }
}

std::shared_ptr<const FakeSnapshot> FakeSnapshot::three_records() {
  // Вымышленные тестовые данные. ИНН 7700000016 — с верной контрольной цифрой (АРХ §7.7), не реальный.
  std::vector<FakeRecord> records{
      {.number = "RUD-CR.PA08.B.89369/26",
       .status = snapshot::Status::kActive,
       .issue_date = ymd(2026, 2, 10),
       .expiry_date = ymd(2031, 2, 9),
       .status_date = ymd(2026, 2, 10),
       .applicant_name = "ООО «ТЕСТОВЫЙ ЗАЯВИТЕЛЬ»",
       .applicant_inn = "7700000016",
       .manufacturer_name = "ТЕСТОВЫЙ ИЗГОТОВИТЕЛЬ",
       .product = "Тестовая продукция",
       .tnved = "8516790000",
       .registry_id = 1},
      {.number = "RUC-RU.AB12.B.00017/24",
       .status = snapshot::Status::kTerminated,
       .issue_date = ymd(2024, 3, 1),
       .expiry_date = ymd(2029, 2, 28),
       .status_date = ymd(2026, 6, 15),
       .applicant_name = "ООО «ТЕСТОВЫЙ ПОСТАВЩИК»",
       .applicant_inn = "7700000016",
       .manufacturer_name = "ТЕСТОВЫЙ ЗАВОД",
       .product = "Тестовая продукция 2",
       .tnved = "9403200000",
       .registry_id = 2},
      {.number = "RUD-RU.XY01.A.12345/21",
       .status = snapshot::Status::kActive,
       .issue_date = ymd(2021, 1, 15),
       .expiry_date = ymd(2024, 1, 14),
       .status_date = ymd(2021, 1, 15),
       .applicant_name = "ИП ТЕСТОВ",
       .applicant_inn = "",
       .manufacturer_name = "ТЕСТОВЫЙ ИЗГОТОВИТЕЛЬ 3",
       .product = "Тестовая продукция 3",
       .tnved = "6109100000",
       .registry_id = 3},
  };
  snapshot::SnapshotMeta meta{.version = 1,
                              .source = "fake",
                              .source_date = ymd(2026, 9, 26),
                              .canon_version = canon::kVersion,
                              .is_demo = true};
  return std::make_shared<const FakeSnapshot>(std::move(records), std::move(meta));
}

snapshot::RecordView FakeSnapshot::record(std::size_t index) const {
  const auto& r = records_.at(index);
  snapshot::RecordView v;
  v.number = r.number;
  v.kind = r.number.starts_with("RUC") ? canon::DocKind::kCertificate : canon::DocKind::kDeclaration;
  v.status = r.status;
  v.issue_date = r.issue_date;
  v.expiry_date = r.expiry_date;
  v.status_date = r.status_date;
  v.applicant_name = r.applicant_name;
  v.applicant_inn = r.applicant_inn;
  v.manufacturer_name = r.manufacturer_name;
  v.product = r.product;
  v.tnved = r.tnved;
  v.registry_id = r.registry_id;
  return v;
}

std::vector<std::uint32_t> FakeSnapshot::by_serial(std::string_view serial, std::uint8_t year) const {
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < records_.size(); ++i) {
    const auto n = parse(records_[i].number);
    if (n && n->serial == serial && n->year == year) {
      out.push_back(static_cast<std::uint32_t>(i));
    }
  }
  return out;
}

std::optional<std::uint32_t> FakeSnapshot::by_registry_id(std::uint64_t registry_id) const {
  if (registry_id == 0) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < records_.size(); ++i) {
    if (records_[i].registry_id == registry_id) {
      return static_cast<std::uint32_t>(i);
    }
  }
  return std::nullopt;
}

verify::Verdict check(const snapshot::Snapshot& snap, const verify::Query& query) {
  using verify::Basis;
  using verify::Level;
  verify::Verdict v;
  v.query = query.text;
  v.data_date = snap.meta().source_date;
  v.snapshot_version = snap.meta().version;
  v.is_demo = snap.meta().is_demo;

  const auto canonical = canonicalize(query.text);
  if (!canonical) {
    v.level = Level::kNotFound;
    v.findings.push_back({Basis::kCalculation, "number.unparsed", "Не удалось распознать номер"});
    return v;
  }
  v.number = *canonical;

  const auto keys = snap.keys();
  const auto [lo, hi] = std::equal_range(keys.begin(), keys.end(), key_hash(*canonical));
  for (auto it = lo; it != hi; ++it) {
    const auto rec = snap.record(static_cast<std::size_t>(it - keys.begin()));
    if (rec.number != *canonical) {
      continue;  // коллизия хэша
    }
    verify::Card card{.kind = rec.kind,
                      .status = rec.status,
                      .issue_date = rec.issue_date,
                      .expiry_date = rec.expiry_date,
                      .status_date = rec.status_date,
                      .applicant_name = std::string{rec.applicant_name},
                      .applicant_inn = std::string{rec.applicant_inn},
                      .manufacturer_name = std::string{rec.manufacturer_name},
                      .product = std::string{rec.product},
                      .tnved = std::string{rec.tnved},
                      .registry_url = "https://pub.fsa.gov.ru/"};
    v.findings.push_back({Basis::kFact, "status." + std::string{snapshot::to_string(rec.status)},
                          "Статус в реестре: " + std::string{snapshot::to_string(rec.status)}});
    const bool expired = rec.expiry_date && *rec.expiry_date < query.today;
    if (expired) {
      v.findings.push_back(
          {Basis::kCalculation, "term.expired", "Срок действия истёк " + format_date(*rec.expiry_date)});
    }
    v.level = (rec.status == snapshot::Status::kActive && !expired) ? Level::kOk : Level::kProblem;
    if (v.level == Level::kProblem) {
      v.findings.push_back(
          {Basis::kRecommendation, "advice.request_new", "Запросите у поставщика действующий документ"});
    }
    // F8 в упрощённом виде: несовпадение ИНН заявителя и поставщика — предупреждение.
    if (query.supplier_inn && !query.supplier_inn->empty() && !rec.applicant_inn.empty() &&
        rec.applicant_inn != *query.supplier_inn) {
      v.findings.push_back({Basis::kCalculation, "supplier.mismatch", "Документ оформлен не на поставщика"});
      if (v.level == Level::kOk) {
        v.level = Level::kWarning;
      }
    }
    v.card = std::move(card);
    return v;
  }
  v.level = Level::kNotFound;
  v.findings.push_back(
      {Basis::kFact, "not_found", "Нет в данных на " + format_date(snap.meta().source_date)});
  return v;
}

Result<std::vector<recog::Found>> recognize(std::span<const std::byte> file, recog::MediaType /*type*/,
                                            const recog::Limits& limits) {
  if (file.size() > limits.max_bytes) {
    return Error{ErrorCode::kFileTooLarge, "Файл больше лимита"};
  }
  std::string text(file.size(), '\0');
  std::ranges::transform(file, text.begin(), [](std::byte b) { return static_cast<char>(b); });
  std::vector<recog::Found> found;
  for (auto& raw : find_numbers(text, 20)) {
    if (canonicalize(raw)) {
      found.push_back({.raw = std::move(raw), .source = recog::Source::kTextLayer});
    }
  }
  if (found.empty()) {
    return Error{ErrorCode::kNumberNotRecognized, "В файле не найден номер документа"};
  }
  return found;
}

std::string format_date(Date date) {
  const std::chrono::year_month_day ymd{date};
  const auto two = [](unsigned v) {
    return std::string{static_cast<char>('0' + v / 10), static_cast<char>('0' + v % 10)};
  };
  return two(static_cast<unsigned>(ymd.day())) + "." + two(static_cast<unsigned>(ymd.month())) + "." +
         std::to_string(static_cast<int>(ymd.year()));
}

}  // namespace sk::fake
