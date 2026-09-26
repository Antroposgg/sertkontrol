#include "sertkontrol/verify/text.hpp"

#include <array>

namespace sk::verify {

std::string format_date(Date date) {
  const std::chrono::year_month_day ymd{date};
  const auto two = [](unsigned v) {
    return std::string{static_cast<char>('0' + (v / 10)), static_cast<char>('0' + (v % 10))};
  };
  return two(static_cast<unsigned>(ymd.day())) + "." + two(static_cast<unsigned>(ymd.month())) + "." +
         std::to_string(static_cast<int>(ymd.year()));
}

std::string_view status_name(snapshot::Status status) noexcept {
  switch (status) {
    case snapshot::Status::kActive:
      return "действует";
    case snapshot::Status::kSuspended:
      return "приостановлен";
    case snapshot::Status::kTerminated:
      return "прекращён";
    case snapshot::Status::kAnnulled:
      return "аннулирован";
    case snapshot::Status::kArchived:
      return "архивный";
    case snapshot::Status::kUnknown:
      break;
  }
  return "не распознан";
}

std::string_view kind_name(canon::DocKind kind) noexcept {
  return kind == canon::DocKind::kCertificate ? "Сертификат" : "Декларация";
}

std::string registry_url(canon::DocKind kind, std::uint64_t registry_id) {
  std::string base = kind == canon::DocKind::kCertificate ? "https://pub.fsa.gov.ru/rss/certificate"
                                                          : "https://pub.fsa.gov.ru/rds/declaration";
  if (registry_id == 0) {
    return base;
  }
  return base + "/view/" + std::to_string(registry_id) + "/common";
}

std::string display_number(std::string_view canonical) {
  if (canonical.size() < 4 || !canonical.starts_with("RU") || canonical[3] != '-') {
    return std::string{canonical};
  }
  const std::string_view letter = canonical[2] == 'C' ? "С" : "Д";
  return "RU " + std::string{letter} + "-" + std::string{canonical.substr(4)};
}

}  // namespace sk::verify
