#include "domain_service.hpp"

#include <drogon/orm/Exception.h>

#include <algorithm>
#include <charconv>
#include <memory>
#include <string>
#include <utility>

#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>

#include "sertkontrol/snapshot/lookup.hpp"
#include "sertkontrol/verify/inn.hpp"

namespace sk::certd {

namespace {

using drogon::orm::DrogonDbException;

Error db_error(const DrogonDbException& e) {
  return Error{ErrorCode::kInternal, std::string{"БД: "} + e.base().what()};
}

Error no_snapshot() {
  return Error{ErrorCode::kSnapshotUnavailable, "данные реестра ещё загружаются, повторите через минуту"};
}

std::string_view level_name(verify::Level l) {
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

std::string via(Channel c, bool file) {
  return std::string{c == Channel::kBot ? "bot_" : "app_"} + (file ? "file" : "text");
}

std::string kind_name(canon::DocKind k) {
  return k == canon::DocKind::kCertificate ? "certificate" : "declaration";
}

PortfolioItem item_from_row(const drogon::orm::Row& r) {
  PortfolioItem item;
  item.id = r["id"].as<std::int64_t>();
  item.doc_key = r["doc_key"].as<std::string>();
  item.doc_kind = r["doc_kind"].as<std::string>() == "certificate" ? canon::DocKind::kCertificate
                                                                   : canon::DocKind::kDeclaration;
  if (!r["sku"].isNull()) {
    item.sku = r["sku"].as<std::string>();
  }
  if (!r["inn"].isNull()) {
    item.supplier_inn = r["inn"].as<std::string>();
  }
  item.last_status =
      snapshot::status_from_string(r["last_status"].as<std::string>()).value_or(snapshot::Status::kUnknown);
  item.last_version = r["last_version"].isNull() ? 0 : r["last_version"].as<std::uint64_t>();
  return item;
}

/// `YYYY-MM-DD` → дата; иначе `nullopt`.
std::optional<Date> parse_iso(std::string_view s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') {
    return std::nullopt;
  }
  int y = 0;
  unsigned m = 0;
  unsigned d = 0;
  const auto num = [](std::string_view part, auto& out) {
    const auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), out);
    return ec == std::errc{} && ptr == part.data() + part.size();
  };
  if (!num(s.substr(0, 4), y) || !num(s.substr(5, 2), m) || !num(s.substr(8, 2), d)) {
    return std::nullopt;
  }
  const std::chrono::year_month_day ymd{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
  if (!ymd.ok()) {
    return std::nullopt;
  }
  return Date{ymd};
}

/// `registry_change.before/after` (C8): `{"status", "expiry_date", "status_date"}` или SQL NULL.
std::optional<DocStateView> parse_state(const drogon::orm::Field& f) {
  if (f.isNull()) {
    return std::nullopt;
  }
  const auto text = f.as<std::string>();
  Json::Value v;
  std::string errors;
  const std::unique_ptr<Json::CharReader> reader{Json::CharReaderBuilder{}.newCharReader()};
  if (!reader->parse(text.data(), text.data() + text.size(), &v, &errors) || !v.isObject()) {
    return std::nullopt;
  }
  DocStateView out;
  out.status = snapshot::status_from_string(v["status"].asString()).value_or(snapshot::Status::kUnknown);
  if (v["expiry_date"].isString()) {
    out.expiry_date = parse_iso(v["expiry_date"].asString());
  }
  if (v["status_date"].isString()) {
    out.status_date = parse_iso(v["status_date"].asString());
  }
  return out;
}

Error forbidden_not_demo() {
  return Error{ErrorCode::kForbidden, "демо-сценарий доступен только в демо-режиме"};
}

}  // namespace

DomainServiceImpl::DomainServiceImpl(drogon::orm::DbClientPtr db, const SnapshotSet& snapshots,
                                     RecognitionPool& recognition, RateLimiter& limiter,
                                     NotifyService& notify, TodayFn today)
    : db_(std::move(db)),
      snapshots_(snapshots),
      recognition_(recognition),
      limiter_(limiter),
      notify_(notify),
      today_(std::move(today)) {
}

drogon::Task<Result<DomainServiceImpl::UserRow>> DomainServiceImpl::ensure_user(std::int64_t max_user_id) {
  try {
    const auto r = co_await db_->execSqlCoro(
        "INSERT INTO app_user (max_user_id) VALUES ($1) "
        "ON CONFLICT (max_user_id) DO UPDATE SET max_user_id = EXCLUDED.max_user_id "
        "RETURNING id, is_demo, demo_stage, consent_at IS NOT NULL AS consented",
        max_user_id);
    const auto& row = r[0];
    co_return UserRow{.id = row["id"].as<std::int64_t>(),
                      .is_demo = row["is_demo"].as<bool>(),
                      .stage = static_cast<DemoStage>(row["demo_stage"].as<int>()),
                      .consented = row["consented"].as<bool>()};
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<Me>> DomainServiceImpl::me(UserContext user) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  try {
    const auto r = co_await db_->execSqlCoro("SELECT count(*) AS n FROM portfolio_item WHERE user_id = $1",
                                             u.value().id);
    co_return Me{.max_user_id = user.max_user_id,
                 .portfolio_count = r[0]["n"].as<std::size_t>(),
                 .is_demo = u.value().is_demo,
                 .demo_stage = u.value().stage,
                 .consented = u.value().consented};
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<Ok>> DomainServiceImpl::give_consent(UserContext user) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  try {
    co_await db_->execSqlCoro("UPDATE app_user SET consent_at = coalesce(consent_at, now()) WHERE id = $1",
                              u.value().id);
    co_return Ok{};
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<CheckResult>> DomainServiceImpl::check_numbers(
    UserContext user, std::vector<std::string> raws, std::chrono::steady_clock::time_point started,
    bool from_file) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  // Снапшот пользователя берётся один раз на запрос и держится до конца (АРХ §7.4).
  const auto snap = snapshot_of(u.value());
  if (!snap) {
    co_return no_snapshot();
  }
  const auto today = today_();
  CheckResult out;
  out.verdicts.reserve(raws.size());
  for (auto& raw : raws) {
    out.verdicts.push_back(
        {.verdict = verify::check(*snap, verify::Query{.text = std::move(raw), .today = today})});
  }
  const auto latency =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
  try {
    // Журнал проверок: строка на номер, общая пачка — id первой строки. check_id — аргумент кнопок бота.
    for (auto& cv : out.verdicts) {
      const auto& v = cv.verdict;
      const auto r = co_await db_->execSqlCoro(
          "INSERT INTO check_log (user_id, via, doc_key, level, latency_ms, batch_id) "
          "VALUES ($1, $2, nullif($3, ''), $4, $5, nullif($6::bigint, 0)) RETURNING id",
          u.value().id, via(user.channel, from_file), v.number, std::string{level_name(v.level)},
          static_cast<std::int32_t>(latency.count()), out.batch_id);
      cv.check_id = r[0]["id"].as<std::int64_t>();
      if (out.batch_id == 0) {
        out.batch_id = cv.check_id;
        co_await db_->execSqlCoro("UPDATE check_log SET batch_id = id WHERE id = $1", cv.check_id);
      }
    }
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
  co_return out;
}

drogon::Task<Result<CheckResult>> DomainServiceImpl::check_text(UserContext user, std::string text) {
  const auto started = std::chrono::steady_clock::now();
  if (!limiter_.try_acquire(user.max_user_id)) {
    co_return Error{ErrorCode::kRateLimited, "не больше 30 проверок в минуту, попробуйте чуть позже"};
  }
  auto raws = canon::find_numbers(text, kMaxNumbersPerMessage);
  if (raws.empty()) {
    co_return Error{
        ErrorCode::kNumberNotRecognized,
        "не нашёл номер документа: пришлите номер вида «ЕАЭС N RU Д-RU.РА01.В.12345/23» или PDF-выписку"};
  }
  co_return co_await check_numbers(user, std::move(raws), started, false);
}

drogon::Task<Result<CheckResult>> DomainServiceImpl::check_file(UserContext user, FileUpload file) {
  const auto started = std::chrono::steady_clock::now();
  if (!limiter_.try_acquire(user.max_user_id)) {
    co_return Error{ErrorCode::kRateLimited, "не больше 30 проверок в минуту, попробуйте чуть позже"};
  }
  auto found = co_await recognition_.recognize(std::move(file.bytes), file.type);
  if (!found) {
    co_return found.error();
  }
  std::vector<std::string> raws;
  std::vector<std::optional<std::string>> links;
  for (auto& f : found.value()) {
    raws.push_back(std::move(f.raw));
    links.push_back(std::move(f.registry_url));
  }
  auto result = co_await check_numbers(user, std::move(raws), started, true);
  if (result) {
    // Ссылка из QR самого документа точнее построенной по registry_id — это факт из выписки.
    for (std::size_t i = 0; i < links.size(); ++i) {
      auto& card = result.value().verdicts[i].verdict.card;
      const auto& link = links[i];
      if (link.has_value() && card.has_value()) {
        card->registry_url = link.value();
      }
    }
  }
  co_return result;
}

drogon::Task<Result<Page<PortfolioItem>>> DomainServiceImpl::list_portfolio(UserContext user,
                                                                            PortfolioFilter filter) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  const auto limit = std::clamp<std::size_t>(filter.limit, 1, kMaxPageSize);
  std::string status;
  if (filter.status) {
    status = std::string{snapshot::to_string(*filter.status)};
  }
  try {
    const auto r = co_await db_->execSqlCoro(
        "SELECT p.id, p.doc_key, p.doc_kind, p.sku, s.inn, p.last_status, p.last_version "
        "FROM portfolio_item p LEFT JOIN supplier s ON s.id = p.supplier_id "
        "WHERE p.user_id = $1 AND ($2::text = '' OR p.last_status = $2) AND ($3::text = '' OR s.inn = $3) "
        "AND p.id > $4::bigint "
        "ORDER BY p.id LIMIT $5::bigint",
        u.value().id, status, filter.supplier_inn.value_or(""), filter.cursor.value_or(0),
        static_cast<std::int64_t>(limit + 1));
    Page<PortfolioItem> page;
    for (const auto& row : r) {
      if (page.items.size() == limit) {
        page.next_cursor = page.items.back().id;
        break;
      }
      page.items.push_back(item_from_row(row));
    }
    co_return page;
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<AddResult>> DomainServiceImpl::add_for_user(UserRow user, AddRequest request,
                                                                bool attach) {
  const auto snap = snapshot_of(user);
  if (!snap) {
    co_return no_snapshot();
  }
  auto verdict = verify::check(*snap, verify::Query{.text = request.number, .today = today_()});
  if (verdict.number.empty()) {
    co_return Error{ErrorCode::kNumberNotRecognized, "не удалось распознать номер документа"};
  }
  if (request.supplier_inn && !request.supplier_inn->empty() && !verify::inn_valid(*request.supplier_inn)) {
    co_return Error{ErrorCode::kInvalidArgument, "ИНН поставщика: неверный формат или контрольная цифра"};
  }
  const auto kind = verdict.number[2] == 'C' ? canon::DocKind::kCertificate : canon::DocKind::kDeclaration;
  const auto status = verdict.card ? verdict.card->status : snapshot::Status::kUnknown;
  try {
    std::int64_t supplier_id = 0;
    if (request.supplier_inn && !request.supplier_inn->empty()) {
      const auto s = co_await db_->execSqlCoro(
          "INSERT INTO supplier (user_id, inn) VALUES ($1, $2) "
          "ON CONFLICT (user_id, inn) DO UPDATE SET inn = EXCLUDED.inn RETURNING id",
          user.id, *request.supplier_inn);
      supplier_id = s[0]["id"].as<std::int64_t>();
    }
    const auto sku = request.sku.value_or("");
    // attach: документ уже на контроле — записываем поставщика, статус и версию оставляем как были
    // (иначе потеряли бы изменение, о котором пользователь ещё не уведомлён).
    const auto* sql =
        attach ? "INSERT INTO portfolio_item (user_id, doc_key, doc_kind, sku, supplier_id, last_status, "
                 "last_version) VALUES ($1, $2, $3, nullif($4, ''), nullif($5::bigint, 0), $6, $7) "
                 "ON CONFLICT ON CONSTRAINT portfolio_item_uniq DO UPDATE SET supplier_id = "
                 "EXCLUDED.supplier_id RETURNING id, last_status, last_version"
               : "INSERT INTO portfolio_item (user_id, doc_key, doc_kind, sku, supplier_id, last_status, "
                 "last_version) VALUES ($1, $2, $3, nullif($4, ''), nullif($5::bigint, 0), $6, $7) "
                 "ON CONFLICT ON CONSTRAINT portfolio_item_uniq DO NOTHING "
                 "RETURNING id, last_status, last_version";
    const auto r = co_await db_->execSqlCoro(sql, user.id, verdict.number, kind_name(kind), sku, supplier_id,
                                             std::string{snapshot::to_string(status)},
                                             static_cast<std::int64_t>(verdict.snapshot_version));
    if (r.empty()) {
      co_return Error{ErrorCode::kConflict, "документ с этим SKU уже на контроле"};
    }
    PortfolioItem item{
        .id = r[0]["id"].as<std::int64_t>(),
        .doc_key = verdict.number,
        .doc_kind = kind,
        .sku = sku.empty() ? std::nullopt : std::optional<std::string>{sku},
        .supplier_inn = supplier_id == 0 ? std::nullopt : request.supplier_inn,
        .last_status = snapshot::status_from_string(r[0]["last_status"].as<std::string>())
                           .value_or(snapshot::Status::kUnknown),
        .last_version = r[0]["last_version"].isNull() ? 0 : r[0]["last_version"].as<std::uint64_t>()};
    co_return AddResult{.item = std::move(item), .verdict = std::move(verdict)};
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<AddResult>> DomainServiceImpl::add_to_portfolio(UserContext user, AddRequest request) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  co_return co_await add_for_user(u.value(), std::move(request), false);
}

drogon::Task<Result<AddResult>> DomainServiceImpl::add_checked(UserContext user, std::int64_t check_id) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  std::string doc_key;
  try {
    // Фильтр по владельцу: чужая проверка неотличима от несуществующей (IDOR).
    const auto r = co_await db_->execSqlCoro(
        "SELECT doc_key FROM check_log WHERE id = $1 AND user_id = $2 AND doc_key IS NOT NULL", check_id,
        u.value().id);
    if (r.empty()) {
      co_return Error{ErrorCode::kNotFound, "проверка не найдена"};
    }
    doc_key = r[0]["doc_key"].as<std::string>();
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
  co_return co_await add_for_user(u.value(), AddRequest{.number = std::move(doc_key)}, false);
}

drogon::Task<Result<AddResult>> DomainServiceImpl::attach_supplier(UserContext user, std::int64_t check_id,
                                                                   std::string supplier_inn) {
  if (!verify::inn_valid(supplier_inn)) {
    co_return Error{ErrorCode::kInvalidArgument, "ИНН поставщика: неверный формат или контрольная цифра"};
  }
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  std::string doc_key;
  try {
    const auto r = co_await db_->execSqlCoro(
        "SELECT doc_key FROM check_log WHERE id = $1 AND user_id = $2 AND doc_key IS NOT NULL", check_id,
        u.value().id);
    if (r.empty()) {
      co_return Error{ErrorCode::kNotFound, "проверка не найдена"};
    }
    doc_key = r[0]["doc_key"].as<std::string>();
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
  co_return co_await add_for_user(
      u.value(), AddRequest{.number = std::move(doc_key), .supplier_inn = std::move(supplier_inn)}, true);
}

drogon::Task<Result<BatchAddResult>> DomainServiceImpl::add_batch(UserContext user, std::int64_t batch_id) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  std::vector<std::string> keys;
  try {
    const auto r = co_await db_->execSqlCoro(
        "SELECT DISTINCT doc_key FROM check_log WHERE batch_id = $1 AND user_id = $2 AND doc_key IS NOT NULL",
        batch_id, u.value().id);
    for (const auto& row : r) {
      keys.push_back(row["doc_key"].as<std::string>());
    }
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
  if (keys.empty()) {
    co_return Error{ErrorCode::kNotFound, "проверка не найдена"};
  }
  BatchAddResult out;
  for (auto& key : keys) {
    const auto r = co_await add_for_user(u.value(), AddRequest{.number = std::move(key)}, false);
    if (r) {
      ++out.added;
    } else if (r.error().code == ErrorCode::kConflict) {
      ++out.already;
    } else {
      co_return r.error();
    }
  }
  co_return out;
}

drogon::Task<Result<Ok>> DomainServiceImpl::remove_from_portfolio(UserContext user, std::int64_t item_id) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  try {
    const auto r = co_await db_->execSqlCoro("DELETE FROM portfolio_item WHERE id = $1 AND user_id = $2",
                                             item_id, u.value().id);
    if (r.affectedRows() == 0) {
      co_return Error{ErrorCode::kNotFound, "запись не найдена"};
    }
    co_return Ok{};
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
}

drogon::Task<Result<DataStatus>> DomainServiceImpl::data_status(UserContext user) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  const auto snap = snapshot_of(u.value());
  if (!snap) {
    co_return no_snapshot();
  }
  const auto& meta = snap->meta();
  co_return DataStatus{
      .version = meta.version,
      .source = meta.source,
      .source_date = meta.source_date,
      .record_count = snap->size(),
      .is_demo = meta.is_demo,
      .demo_stage = u.value().stage,
      .demo_update_available = u.value().is_demo && snapshots_.get(SnapshotRole::kDemoUpdated) != nullptr};
}

drogon::Task<Result<DocumentHistory>> DomainServiceImpl::history(UserContext user, std::string number) {
  const auto canonical = canon::canonicalize(number);
  if (!canonical) {
    co_return Error{ErrorCode::kNumberNotRecognized, "не удалось распознать номер документа"};
  }
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  const auto snap = snapshot_of(u.value());
  if (!snap) {
    co_return no_snapshot();
  }
  DocumentHistory out{.doc_key = *canonical};
  // Видимые пользователю версии: боевые — все не новее загруженной; демо — только изменения N → N+1 и только
  // на стадии N+1 (на стадии N обновление ещё «не наступило»).
  const bool demo = u.value().is_demo;
  if (demo && u.value().stage != DemoStage::kUpdated) {
    co_return out;
  }
  const auto visible = static_cast<std::int64_t>(snap->meta().version);
  try {
    const auto r = co_await db_->execSqlCoro(
        "SELECT c.version, v.source_date::text AS source_date, c.before::text AS before, c.after::text AS "
        "after "
        "FROM registry_change c JOIN snapshot_version v ON v.version = c.version "
        "WHERE c.doc_key = $1 AND v.status = 'ready' AND v.is_demo = $2::boolean "
        "AND (($2::boolean AND c.version = $3::bigint) OR (NOT $2::boolean AND c.version <= $3::bigint)) "
        "ORDER BY c.version DESC LIMIT 100",
        *canonical, demo, visible);
    for (const auto& row : r) {
      out.entries.push_back(
          HistoryEntry{.version = row["version"].as<std::uint64_t>(),
                       .data_date = parse_iso(row["source_date"].as<std::string>()).value_or(Date{}),
                       .before = parse_state(row["before"]),
                       .after = parse_state(row["after"])});
    }
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
  co_return out;
}

drogon::Task<Result<DemoUpdate>> DomainServiceImpl::simulate_update(UserContext user) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  if (!u.value().is_demo) {
    co_return forbidden_not_demo();
  }
  auto next = snapshots_.get(SnapshotRole::kDemoUpdated);
  if (!next) {
    co_return Error{ErrorCode::kSnapshotUnavailable,
                    "демо-снапшот N+1 ещё не загружен, повторите через минуту"};
  }
  try {
    co_await db_->execSqlCoro("UPDATE app_user SET demo_stage = 1 WHERE id = $1", u.value().id);
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
  // Портфель пользователя против N+1 — тот же алгоритм и та же идемпотентность, что у боевой версии (F5).
  const auto stats = co_await notify_.notify_user(std::move(next), u.value().id);
  if (!stats) {
    co_return stats.error();
  }
  co_return DemoUpdate{.notified = stats.value().notified};
}

drogon::Task<Result<Ok>> DomainServiceImpl::reset_demo(UserContext user) {
  const auto u = co_await ensure_user(user.max_user_id);
  if (!u) {
    co_return u.error();
  }
  if (!u.value().is_demo) {
    co_return forbidden_not_demo();
  }
  const auto base = snapshots_.get(SnapshotRole::kDemoBase);
  if (!base) {
    co_return no_snapshot();
  }
  const auto updated = snapshots_.get(SnapshotRole::kDemoUpdated);
  const auto updated_version = updated ? static_cast<std::int64_t>(updated->meta().version) : 0;
  try {
    // Статусы портфеля — как если бы всё поставили на контроль на стадии N.
    const auto r =
        co_await db_->execSqlCoro("SELECT id, doc_key FROM portfolio_item WHERE user_id = $1", u.value().id);
    Json::Value items{Json::arrayValue};
    for (const auto& row : r) {
      const auto key = row["doc_key"].as<std::string>();
      auto status = snapshot::Status::kUnknown;
      if (const auto idx = snapshot::find_index(*base, key)) {
        status = base->record(*idx).status;
      }
      Json::Value item{Json::objectValue};
      item["id"] = static_cast<Json::Int64>(row["id"].as<std::int64_t>());
      item["status"] = std::string{snapshot::to_string(status)};
      items.append(item);
    }
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    // Одна инструкция: статусы, удаление уведомлений о N+1 и стадия меняются вместе.
    co_await db_->execSqlCoro(
        "WITH x AS (SELECT * FROM jsonb_to_recordset($2::jsonb) AS x(id bigint, status text)), "
        "upd AS (UPDATE portfolio_item p SET last_status = x.status, last_version = $3::bigint FROM x "
        "  WHERE p.id = x.id AND p.user_id = $1 RETURNING p.id), "
        "del AS (DELETE FROM notification n USING portfolio_item p "
        "  WHERE n.portfolio_item_id = p.id AND p.user_id = $1 AND n.version = $4::bigint RETURNING n.id) "
        "UPDATE app_user SET demo_stage = 0 WHERE id = $1",
        u.value().id, Json::writeString(b, items), static_cast<std::int64_t>(base->meta().version),
        updated_version);
    co_return Ok{};
  } catch (const DrogonDbException& e) {
    co_return db_error(e);
  }
}

}  // namespace sk::certd
