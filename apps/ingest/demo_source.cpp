#include "demo_source.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <string>
#include <vector>

#include "normalize.hpp"

namespace sk::ingest {

namespace {

constexpr std::array<std::string_view, 14> kColumns{
    "number",          "status",         "issue_date",        "expiry_date",       "status_date",
    "suspended_until", "applicant_name", "applicant_inn",     "manufacturer_name", "product",
    "tnved",           "country",        "lab_accreditation", "registry_id"};

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    const auto tab = line.find('\t', start);
    out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos) {
      return out;
    }
    start = tab + 1;
  }
}

void strip_cr(std::string& line) {
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
}

}  // namespace

Result<DemoTsvSource> DemoTsvSource::open(const std::filesystem::path& file) {
  std::ifstream in{file};
  if (!in) {
    return Error{ErrorCode::kNotFound, "нет файла демо-данных " + file.string()};
  }
  std::optional<Date> date;
  std::string line;
  while (std::getline(in, line)) {
    strip_cr(line);
    if (line.starts_with("#source_date=")) {
      const auto parsed = parse_iso_date(std::string_view{line}.substr(13));
      if (!parsed.ok || !parsed.date) {
        return Error{ErrorCode::kInvalidArgument, "некорректная #source_date в " + file.string()};
      }
      date = parsed.date;
      continue;
    }
    if (line.empty() || line.starts_with('#')) {
      continue;
    }
    // Первая строка без «#» — имена колонок.
    const auto header = split_tabs(line);
    if (!std::ranges::equal(header, kColumns)) {
      return Error{ErrorCode::kInvalidArgument, "неожиданные колонки в " + file.string()};
    }
    if (!date) {
      return Error{ErrorCode::kInvalidArgument, "нет строки #source_date= в " + file.string()};
    }
    return DemoTsvSource{file, *date};
  }
  return Error{ErrorCode::kInvalidArgument, "нет заголовка колонок в " + file.string()};
}

Result<std::size_t> DemoTsvSource::for_each(const RecordSink& sink) const {
  std::ifstream in{file_};
  if (!in) {
    return Error{ErrorCode::kNotFound, "нет файла демо-данных " + file_.string()};
  }
  std::string line;
  bool header_seen = false;
  std::size_t count = 0;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    strip_cr(line);
    if (line.empty() || line.starts_with('#')) {
      continue;
    }
    if (!header_seen) {
      header_seen = true;
      continue;
    }
    auto f = split_tabs(line);
    if (f.size() != kColumns.size()) {
      return Error{ErrorCode::kInvalidArgument, file_.filename().string() + ":" + std::to_string(line_no) +
                                                    ": ожидается " + std::to_string(kColumns.size()) +
                                                    " колонок, получено " + std::to_string(f.size())};
    }
    RawRecord r;
    r.number = std::move(f[0]);
    r.status = std::move(f[1]);
    r.issue_date = std::move(f[2]);
    r.expiry_date = std::move(f[3]);
    r.status_date = std::move(f[4]);
    r.suspended_until = std::move(f[5]);
    r.applicant_name = std::move(f[6]);
    r.applicant_inn = std::move(f[7]);
    r.manufacturer_name = std::move(f[8]);
    r.product = std::move(f[9]);
    r.tnved = std::move(f[10]);
    r.country = std::move(f[11]);
    r.lab_accreditation = std::move(f[12]);
    const auto& id = f[13];
    const auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), r.registry_id);
    if (ec != std::errc{} || ptr != id.data() + id.size()) {
      return Error{ErrorCode::kInvalidArgument,
                   file_.filename().string() + ":" + std::to_string(line_no) + ": некорректный registry_id"};
    }
    sink(std::move(r));
    ++count;
  }
  return count;
}

}  // namespace sk::ingest
