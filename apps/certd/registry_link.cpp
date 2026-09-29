#include "registry_link.hpp"

namespace sk::certd {

Result<std::string> number_by_registry_link(const snapshot::Snapshot& snap, std::string_view text) {
  const auto ref = verify::parse_registry_url(text);
  if (!ref) {
    return Error{
        ErrorCode::kNumberNotRecognized,
        "не нашёл номер документа: пришлите номер вида «ЕАЭС N RU Д-RU.РА01.В.12345/23» или PDF-выписку"};
  }
  const auto index = snap.by_registry_id(ref->registry_id);
  // Вид документа из ссылки должен совпасть с записью: ID деклараций и сертификатов — разные реестры.
  if (index.has_value()) {
    const auto rec = snap.record(*index);
    if (rec.kind == ref->kind) {
      return std::string{rec.number};
    }
  }
  return Error{ErrorCode::kNotFoundInSnapshot, "записи реестра по этой ссылке нет в данных на " +
                                                   verify::format_date(snap.meta().source_date)};
}

}  // namespace sk::certd
