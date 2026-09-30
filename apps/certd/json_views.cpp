#include "json_views.hpp"

#include "sertkontrol/verify/text.hpp"

namespace sk::certd {

namespace {

std::string_view basis_json(verify::Basis b) {
  switch (b) {
    case verify::Basis::kFact:
      return "fact";
    case verify::Basis::kCalculation:
      return "calculation";
    case verify::Basis::kRecommendation:
      return "recommendation";
  }
  return "fact";
}

std::string_view level_json(verify::Level l) {
  switch (l) {
    case verify::Level::kOk:
      return "ok";
    case verify::Level::kWarning:
      return "warning";
    case verify::Level::kProblem:
      return "problem";
    case verify::Level::kNotFound:
      return "not_found";
    case verify::Level::kNeedsConfirmation:
      return "needs_confirmation";
  }
  return "not_found";
}

std::string_view kind_json(canon::DocKind k) {
  return k == canon::DocKind::kCertificate ? "certificate" : "declaration";
}

Json::Value date_or_null(const std::optional<Date>& d) {
  return d ? Json::Value{iso_date(*d)} : Json::Value{};
}

std::string_view title(ErrorCode code) {
  switch (code) {
    case ErrorCode::kInvalidArgument:
      return "Некорректный запрос";
    case ErrorCode::kUnauthorized:
      return "Требуется авторизация";
    case ErrorCode::kInitDataExpired:
      return "Сессия устарела";
    case ErrorCode::kForbidden:
      return "Действие недоступно";
    case ErrorCode::kNotFound:
      return "Не найдено";
    case ErrorCode::kConflict:
      return "Уже существует";
    case ErrorCode::kFileTooLarge:
      return "Файл слишком большой";
    case ErrorCode::kUnsupportedMediaType:
      return "Неподдерживаемый файл";
    case ErrorCode::kNumberNotRecognized:
      return "Номер не распознан";
    case ErrorCode::kNotFoundInSnapshot:
      return "Нет в данных";
    case ErrorCode::kSnapshotUnavailable:
      return "Данные недоступны";
    case ErrorCode::kRateLimited:
      return "Слишком много запросов";
    case ErrorCode::kConsentRequired:
      return "Нужно согласие";
    case ErrorCode::kInternal:
      return "Внутренняя ошибка";
  }
  return "Внутренняя ошибка";
}

}  // namespace

int http_status(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kInvalidArgument:
      return 400;
    case ErrorCode::kUnauthorized:
    case ErrorCode::kInitDataExpired:
      return 401;
    case ErrorCode::kForbidden:
    case ErrorCode::kConsentRequired:
      return 403;
    case ErrorCode::kNotFound:
    case ErrorCode::kNotFoundInSnapshot:
      return 404;
    case ErrorCode::kConflict:
      return 409;
    case ErrorCode::kFileTooLarge:
      return 413;
    case ErrorCode::kUnsupportedMediaType:
      return 415;
    case ErrorCode::kNumberNotRecognized:
      return 422;
    case ErrorCode::kRateLimited:
      return 429;
    case ErrorCode::kSnapshotUnavailable:
      return 503;
    case ErrorCode::kInternal:
      return 500;
  }
  return 500;
}

Json::Value problem_json(const Error& error) {
  Json::Value v{Json::objectValue};
  v["type"] = "https://sertkontrol.local/problems/" + std::string{to_string(error.code)};
  v["title"] = std::string{title(error.code)};
  v["status"] = http_status(error.code);
  v["detail"] = error.detail;
  v["code"] = std::string{to_string(error.code)};
  return v;
}

std::string iso_date(Date d) {
  const std::chrono::year_month_day ymd{d};
  const auto two = [](unsigned x) { return (x < 10 ? "0" : "") + std::to_string(x); };
  return std::to_string(static_cast<int>(ymd.year())) + "-" + two(static_cast<unsigned>(ymd.month())) + "-" +
         two(static_cast<unsigned>(ymd.day()));
}

Json::Value to_json(const verify::Verdict& v) {
  Json::Value j{Json::objectValue};
  j["query"] = v.query;
  j["number"] = v.number.empty() ? Json::Value{} : Json::Value{v.number};
  j["display_number"] = v.number.empty() ? Json::Value{} : Json::Value{verify::display_number(v.number)};
  j["level"] = std::string{level_json(v.level)};
  j["data_date"] = iso_date(v.data_date);
  j["snapshot_version"] = static_cast<Json::UInt64>(v.snapshot_version);
  j["is_demo"] = v.is_demo;
  j["distance"] = v.distance;
  if (v.card) {
    const auto& c = *v.card;
    Json::Value card{Json::objectValue};
    card["kind"] = std::string{kind_json(c.kind)};
    card["status"] = std::string{snapshot::to_string(c.status)};
    card["status_name"] = std::string{verify::status_name(c.status)};
    card["issue_date"] = date_or_null(c.issue_date);
    card["expiry_date"] = date_or_null(c.expiry_date);
    card["status_date"] = date_or_null(c.status_date);
    card["applicant_name"] = c.applicant_name;
    card["applicant_inn"] = c.applicant_inn;
    card["manufacturer_name"] = c.manufacturer_name;
    card["product"] = c.product;
    card["tnved"] = c.tnved;
    card["registry_url"] = c.registry_url;
    j["card"] = card;
  } else {
    j["card"] = Json::Value{};
  }
  j["findings"] = Json::Value{Json::arrayValue};
  for (const auto& f : v.findings) {
    Json::Value jf{Json::objectValue};
    jf["basis"] = std::string{basis_json(f.basis)};
    jf["rule"] = f.rule;
    jf["text"] = f.text;
    j["findings"].append(jf);
  }
  j["suggestions"] = Json::Value{Json::arrayValue};
  for (const auto& s : v.suggestions) {
    Json::Value js{Json::objectValue};
    js["number"] = s.number;
    js["display_number"] = verify::display_number(s.number);
    js["distance"] = s.distance;
    j["suggestions"].append(js);
  }
  return j;
}

Json::Value to_json(const CheckedVerdict& v) {
  auto j = to_json(v.verdict);
  j["check_id"] = static_cast<Json::Int64>(v.check_id);
  return j;
}

Json::Value to_json(const PortfolioItem& item) {
  Json::Value j{Json::objectValue};
  j["id"] = static_cast<Json::Int64>(item.id);
  j["doc_key"] = item.doc_key;
  j["display_number"] = verify::display_number(item.doc_key);
  j["doc_kind"] = std::string{kind_json(item.doc_kind)};
  j["sku"] = item.sku ? Json::Value{*item.sku} : Json::Value{};
  j["supplier_inn"] = item.supplier_inn ? Json::Value{*item.supplier_inn} : Json::Value{};
  j["last_status"] = std::string{snapshot::to_string(item.last_status)};
  j["last_version"] = static_cast<Json::UInt64>(item.last_version);
  return j;
}

Json::Value to_json(const Page<PortfolioItem>& page) {
  Json::Value j{Json::objectValue};
  j["items"] = Json::Value{Json::arrayValue};
  for (const auto& i : page.items) {
    j["items"].append(to_json(i));
  }
  j["next_cursor"] = page.next_cursor ? Json::Value{std::to_string(*page.next_cursor)} : Json::Value{};
  return j;
}

Json::Value to_json(const AddResult& r) {
  Json::Value j{Json::objectValue};
  j["item"] = to_json(r.item);
  j["verdict"] = to_json(r.verdict);
  return j;
}

Json::Value to_json(const Me& me) {
  Json::Value j{Json::objectValue};
  j["max_user_id"] = static_cast<Json::Int64>(me.max_user_id);
  j["portfolio_count"] = static_cast<Json::UInt64>(me.portfolio_count);
  j["is_demo"] = me.is_demo;
  j["demo_stage"] = me.demo_stage == DemoStage::kUpdated ? "updated" : "base";
  j["consented"] = me.consented;
  return j;
}

Json::Value to_json(const DataStatus& s) {
  Json::Value j{Json::objectValue};
  j["version"] = static_cast<Json::UInt64>(s.version);
  j["source"] = s.source;
  j["source_date"] = iso_date(s.source_date);
  j["record_count"] = static_cast<Json::UInt64>(s.record_count);
  j["is_demo"] = s.is_demo;
  j["demo_stage"] = s.demo_stage == DemoStage::kUpdated ? "updated" : "base";
  j["demo_update_available"] = s.demo_update_available;
  // Ежедневное обновление запускается, но источник ФСА не подтверждён — момента следующих данных нет.
  j["next_update"] = Json::Value{};
  return j;
}

namespace {

Json::Value state_json(const std::optional<DocStateView>& s) {
  if (!s) {
    return Json::Value{};
  }
  Json::Value j{Json::objectValue};
  j["status"] = std::string{snapshot::to_string(s->status)};
  j["status_name"] = std::string{verify::status_name(s->status)};
  j["expiry_date"] = date_or_null(s->expiry_date);
  j["status_date"] = date_or_null(s->status_date);
  return j;
}

}  // namespace

Json::Value to_json(const DocumentHistory& h) {
  Json::Value j{Json::objectValue};
  j["doc_key"] = h.doc_key;
  j["display_number"] = verify::display_number(h.doc_key);
  j["entries"] = Json::Value{Json::arrayValue};
  for (const auto& e : h.entries) {
    Json::Value je{Json::objectValue};
    je["version"] = static_cast<Json::UInt64>(e.version);
    je["data_date"] = iso_date(e.data_date);
    je["before"] = state_json(e.before);
    je["after"] = state_json(e.after);
    j["entries"].append(je);
  }
  return j;
}

Json::Value to_json(const ImportReport& r) {
  const auto lines = [](const std::vector<ImportedLine>& in) {
    Json::Value a{Json::arrayValue};
    for (const auto& l : in) {
      Json::Value j{Json::objectValue};
      j["line"] = static_cast<Json::UInt64>(l.line);
      j["number"] = l.number;
      j["display_number"] = verify::display_number(l.number);
      a.append(j);
    }
    return a;
  };
  Json::Value j{Json::objectValue};
  j["total"] = static_cast<Json::UInt64>(r.total);
  j["added"] = static_cast<Json::UInt64>(r.added);
  j["already"] = static_cast<Json::UInt64>(r.already);
  j["not_found"] = lines(r.not_found);
  j["supplier_mismatch"] = lines(r.supplier_mismatch);
  j["invalid"] = Json::Value{Json::arrayValue};
  for (const auto& i : r.invalid) {
    Json::Value ji{Json::objectValue};
    ji["line"] = static_cast<Json::UInt64>(i.line);
    ji["reason"] = i.reason;
    j["invalid"].append(ji);
  }
  return j;
}

Json::Value to_json(const DemoUpdate& u) {
  Json::Value j{Json::objectValue};
  j["notified"] = static_cast<Json::UInt64>(u.notified);
  return j;
}

}  // namespace sk::certd
