#include "import_report.hpp"

#include <algorithm>

namespace sk::certd {

std::optional<Error> account_import_row(ImportReport& report, const ImportRow& row,
                                        const Result<AddResult>& added) {
  if (added) {
    ++report.added;
    const auto& v = added.value().verdict;
    if (v.level == verify::Level::kNotFound || v.level == verify::Level::kNeedsConfirmation) {
      report.not_found.push_back({.line = row.line, .number = v.number});
    }
    if (std::ranges::any_of(v.findings,
                            [](const verify::Finding& f) { return f.rule == "supplier.mismatch"; })) {
      report.supplier_mismatch.push_back({.line = row.line, .number = v.number});
    }
    return std::nullopt;
  }
  switch (added.error().code) {
    case ErrorCode::kConflict:
      ++report.already;
      return std::nullopt;
    case ErrorCode::kNumberNotRecognized:
      report.invalid.push_back({.line = row.line, .reason = "номер документа не распознан"});
      return std::nullopt;
    case ErrorCode::kInvalidArgument:
      report.invalid.push_back({.line = row.line, .reason = added.error().detail});
      return std::nullopt;
    default:
      return added.error();
  }
}

AddRequest import_request(const ImportRow& row) {
  AddRequest request{.number = row.number};
  if (!row.sku.empty()) {
    request.sku = row.sku;
  }
  if (!row.supplier_inn.empty()) {
    request.supplier_inn = row.supplier_inn;
  }
  return request;
}

void finish_import_report(ImportReport& report) {
  std::ranges::stable_sort(report.invalid, {}, &ImportIssue::line);
}

}  // namespace sk::certd
