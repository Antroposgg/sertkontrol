#include "normalize.hpp"

#include <array>
#include <charconv>
#include <utility>

namespace sk::ingest {

namespace {

/// Нижний регистр для ASCII и кириллицы (UTF-8 двухбайтовые А–Я, Ё), «ё» → «е».
std::string fold(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c + ('a' - 'A')));
      continue;
    }
    if (i + 1 < s.size()) {
      const auto c2 = static_cast<unsigned char>(s[i + 1]);
      // А–П: D0 90–9F → D0 B0–BF; Р–Я: D0 A0–AF → D1 80–8F; Ё: D0 81, ё: D1 91 → е: D0 B5.
      if (c == 0xD0 && c2 >= 0x90 && c2 <= 0x9F) {
        out += {static_cast<char>(0xD0), static_cast<char>(c2 + 0x20)};
        ++i;
        continue;
      }
      if (c == 0xD0 && c2 >= 0xA0 && c2 <= 0xAF) {
        out += {static_cast<char>(0xD1), static_cast<char>(c2 - 0x20)};
        ++i;
        continue;
      }
      if ((c == 0xD0 && c2 == 0x81) || (c == 0xD1 && c2 == 0x91)) {
        out += {static_cast<char>(0xD0), static_cast<char>(0xB5)};
        ++i;
        continue;
      }
    }
    out.push_back(static_cast<char>(c));
  }
  // Без пробелов по краям.
  const auto b = out.find_first_not_of(" \t");
  const auto e = out.find_last_not_of(" \t");
  return b == std::string::npos ? std::string{} : out.substr(b, e - b + 1);
}

}  // namespace

std::optional<snapshot::Status> status_from_source(std::string_view text) {
  using snapshot::Status;
  // Формулировки реестров Росаккредитации; «возобновлён» — снова действует.
  static constexpr std::array<std::pair<std::string_view, Status>, 8> kMap{{
      {"действует", Status::kActive},
      {"возобновлен", Status::kActive},
      {"приостановлен", Status::kSuspended},
      {"прекращен", Status::kTerminated},
      {"аннулирован", Status::kAnnulled},
      {"архивный", Status::kArchived},
      {"частично приостановлен", Status::kSuspended},
      {"продлен", Status::kActive},
  }};
  const auto key = fold(text);
  for (const auto& [name, status] : kMap) {
    if (key == name) {
      return status;
    }
  }
  return std::nullopt;
}

ParsedDate parse_iso_date(std::string_view text) {
  if (text.empty()) {
    return {.ok = true};
  }
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    return {};
  }
  const auto num = [&](std::size_t pos, std::size_t len, int& out) {
    const auto* first = text.data() + pos;
    const auto [ptr, ec] = std::from_chars(first, first + len, out);
    return ec == std::errc{} && ptr == first + len;
  };
  int y = 0;
  int m = 0;
  int d = 0;
  if (!num(0, 4, y) || !num(5, 2, m) || !num(8, 2, d)) {
    return {};
  }
  const std::chrono::year_month_day ymd{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)},
                                        std::chrono::day{static_cast<unsigned>(d)}};
  if (!ymd.ok()) {
    return {};
  }
  return {.ok = true, .date = Date{ymd}};
}

Result<snapshot::RecordInput> normalize(const RawRecord& raw) {
  const auto canonical = canon::canonicalize(raw.number);
  if (!canonical) {
    return Error{ErrorCode::kInvalidArgument, "номер не распознан"};
  }
  const auto status = status_from_source(raw.status);
  if (!status) {
    return Error{ErrorCode::kInvalidArgument, "неизвестный статус «" + raw.status + "»"};
  }
  snapshot::RecordInput r;
  r.number = *canonical;
  r.status = *status;
  const std::array<std::pair<const std::string*, std::optional<Date>*>, 4> dates{{
      {&raw.issue_date, &r.issue_date},
      {&raw.expiry_date, &r.expiry_date},
      {&raw.status_date, &r.status_date},
      {&raw.suspended_until, &r.suspended_until},
  }};
  for (const auto& [text, target] : dates) {
    const auto parsed = parse_iso_date(*text);
    if (!parsed.ok) {
      return Error{ErrorCode::kInvalidArgument, "некорректная дата «" + *text + "»"};
    }
    *target = parsed.date;
  }
  r.applicant_name = raw.applicant_name;
  r.applicant_inn = raw.applicant_inn;
  r.manufacturer_name = raw.manufacturer_name;
  r.product = raw.product;
  r.tnved = raw.tnved;
  r.country = raw.country;
  r.lab_accreditation = raw.lab_accreditation;
  r.registry_id = raw.registry_id;
  return r;
}

}  // namespace sk::ingest
