#include "fake_domain.hpp"

#include <algorithm>
#include <utility>

#include "sertkontrol/fakes.hpp"

namespace sk::certd {

FakeDomainService::FakeDomainService(snapshot::SnapshotPtr base, snapshot::SnapshotPtr updated, Date today)
    : base_(std::move(base)), updated_(std::move(updated)), today_(today) {
}

snapshot::SnapshotPtr FakeDomainService::snapshot_for(std::int64_t max_user_id) const {
  const std::scoped_lock lock(mutex_);
  const auto it = stages_.find(max_user_id);
  return (it != stages_.end() && it->second == DemoStage::kUpdated) ? updated_ : base_;
}

std::vector<verify::Verdict> FakeDomainService::check_all(std::int64_t max_user_id,
                                                          const std::vector<std::string>& raws) const {
  // Снапшот берётся один раз на запрос (АРХ §7.4).
  const auto snap = snapshot_for(max_user_id);
  std::vector<verify::Verdict> out;
  out.reserve(raws.size());
  for (const auto& raw : raws) {
    out.push_back(fake::check(*snap, verify::Query{.text = raw, .today = today_}));
  }
  return out;
}

drogon::Task<Result<Me>> FakeDomainService::me(UserContext user) {
  const std::scoped_lock lock(mutex_);
  Me me{.max_user_id = user.max_user_id};
  me.portfolio_count = static_cast<std::size_t>(
      std::ranges::count_if(items_, [&](const auto& kv) { return kv.second.owner == user.max_user_id; }));
  if (const auto it = stages_.find(user.max_user_id); it != stages_.end()) {
    me.demo_stage = it->second;
  }
  co_return me;
}

drogon::Task<Result<std::vector<verify::Verdict>>> FakeDomainService::check_text(UserContext user,
                                                                                 std::string text) {
  const auto raws = fake::find_numbers(text, kMaxNumbersPerMessage);
  if (raws.empty()) {
    co_return Error{ErrorCode::kNumberNotRecognized, "В сообщении не найден номер документа"};
  }
  co_return check_all(user.max_user_id, raws);
}

drogon::Task<Result<std::vector<verify::Verdict>>> FakeDomainService::check_file(UserContext user,
                                                                                 FileUpload file) {
  auto found = fake::recognize(file.bytes, file.type, recog::Limits{});
  if (!found) {
    co_return found.error();
  }
  std::vector<std::string> raws;
  for (auto& f : found.value()) {
    raws.push_back(std::move(f.raw));
  }
  co_return check_all(user.max_user_id, raws);
}

drogon::Task<Result<Page<PortfolioItem>>> FakeDomainService::list_portfolio(UserContext user,
                                                                            PortfolioFilter filter) {
  const std::scoped_lock lock(mutex_);
  Page<PortfolioItem> page;
  auto it = filter.cursor ? items_.upper_bound(*filter.cursor) : items_.begin();
  for (; it != items_.end(); ++it) {
    const auto& [id, stored] = *it;
    if (stored.owner != user.max_user_id) {
      continue;
    }
    if (filter.status && stored.item.last_status != *filter.status) {
      continue;
    }
    if (filter.supplier_inn && stored.item.supplier_inn != filter.supplier_inn) {
      continue;
    }
    if (page.items.size() == filter.limit) {
      page.next_cursor = page.items.back().id;
      break;
    }
    page.items.push_back(stored.item);
  }
  co_return page;
}

drogon::Task<Result<AddResult>> FakeDomainService::add_to_portfolio(UserContext user, AddRequest request) {
  auto verdicts = check_all(user.max_user_id, {request.number});
  auto& verdict = verdicts.front();
  if (verdict.number.empty()) {
    co_return Error{ErrorCode::kNumberNotRecognized, "Не удалось распознать номер"};
  }
  const std::scoped_lock lock(mutex_);
  const bool duplicate = std::ranges::any_of(items_, [&](const auto& kv) {
    return kv.second.owner == user.max_user_id && kv.second.item.doc_key == verdict.number &&
           kv.second.item.sku == request.sku;
  });
  if (duplicate) {
    co_return Error{ErrorCode::kConflict, "Документ с этим SKU уже на контроле"};
  }
  PortfolioItem item{.id = next_id_++,
                     .doc_key = verdict.number,
                     .doc_kind = verdict.card ? verdict.card->kind : canon::DocKind::kDeclaration,
                     .sku = std::move(request.sku),
                     .supplier_inn = std::move(request.supplier_inn),
                     .last_status = verdict.card ? verdict.card->status : snapshot::Status::kUnknown,
                     .last_version = verdict.snapshot_version};
  items_.emplace(item.id, StoredItem{.owner = user.max_user_id, .item = item});
  co_return AddResult{.item = std::move(item), .verdict = std::move(verdict)};
}

drogon::Task<Result<Ok>> FakeDomainService::remove_from_portfolio(UserContext user, std::int64_t item_id) {
  const std::scoped_lock lock(mutex_);
  const auto it = items_.find(item_id);
  // Чужая запись неотличима от несуществующей — не раскрываем факт её наличия (IDOR).
  if (it == items_.end() || it->second.owner != user.max_user_id) {
    co_return Error{ErrorCode::kNotFound, "Запись не найдена"};
  }
  items_.erase(it);
  co_return Ok{};
}

drogon::Task<Result<DataStatus>> FakeDomainService::data_status(UserContext user) {
  const auto snap = snapshot_for(user.max_user_id);
  const auto& meta = snap->meta();
  co_return DataStatus{.version = meta.version,
                       .source = meta.source,
                       .source_date = meta.source_date,
                       .record_count = snap->size(),
                       .is_demo = meta.is_demo};
}

drogon::Task<Result<Ok>> FakeDomainService::simulate_update(UserContext user) {
  const std::scoped_lock lock(mutex_);
  stages_[user.max_user_id] = DemoStage::kUpdated;
  co_return Ok{};
}

drogon::Task<Result<Ok>> FakeDomainService::reset_demo(UserContext user) {
  const std::scoped_lock lock(mutex_);
  stages_[user.max_user_id] = DemoStage::kBase;
  co_return Ok{};
}

}  // namespace sk::certd
