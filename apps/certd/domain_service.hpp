/// @file domain_service.hpp
/// @brief `DomainServiceImpl` — C6 на PostgreSQL (пользователи, портфель, журнал проверок) и снапшоте
/// (вердикт).
#pragma once

#include <drogon/orm/DbClient.h>

#include <chrono>
#include <functional>
#include <memory>

#include "domain.hpp"
#include "notify.hpp"
#include "rate_limiter.hpp"
#include "recognition_pool.hpp"
#include "snapshot_set.hpp"

namespace sk::certd {

/// «Сегодня» для правил срока; инъекция для тестов.
using TodayFn = std::function<Date()>;

/// Реализация C6. Каждый запрос к БД фильтруется по `user_id`, полученному из `max_user_id`
/// проверенного initData или события webhook (АРХ §10, IDOR). Все запросы параметризованы.
/// Снапшот выбирается по пользователю: демо-стадия N или N+1, боевой — для не-демо (АРХ §4).
class DomainServiceImpl final : public DomainService {
 public:
  DomainServiceImpl(drogon::orm::DbClientPtr db, const SnapshotSet& snapshots, RecognitionPool& recognition,
                    RateLimiter& limiter, NotifyService& notify, TodayFn today);

  drogon::Task<Result<Me>> me(UserContext user) override;
  drogon::Task<Result<Ok>> give_consent(UserContext user) override;
  drogon::Task<Result<CheckResult>> check_text(UserContext user, std::string text) override;
  drogon::Task<Result<CheckResult>> check_file(UserContext user, FileUpload file) override;
  drogon::Task<Result<Page<PortfolioItem>>> list_portfolio(UserContext user, PortfolioFilter filter) override;
  drogon::Task<Result<AddResult>> add_to_portfolio(UserContext user, AddRequest request) override;
  drogon::Task<Result<AddResult>> add_checked(UserContext user, std::int64_t check_id) override;
  drogon::Task<Result<CheckResult>> confirm(UserContext user, std::int64_t check_id) override;
  drogon::Task<Result<BatchAddResult>> add_batch(UserContext user, std::int64_t batch_id) override;
  drogon::Task<Result<ImportReport>> import_portfolio(UserContext user, std::string csv) override;
  drogon::Task<Result<AddResult>> attach_supplier(UserContext user, std::int64_t check_id,
                                                  std::string supplier_inn) override;
  drogon::Task<Result<Ok>> remove_from_portfolio(UserContext user, std::int64_t item_id) override;
  drogon::Task<Result<DataStatus>> data_status(UserContext user) override;
  drogon::Task<Result<DocumentHistory>> history(UserContext user, std::string number) override;
  drogon::Task<Result<DemoUpdate>> simulate_update(UserContext user) override;
  drogon::Task<Result<Ok>> reset_demo(UserContext user) override;

  /// Сколько номеров максимум проверяется из одного текста (F1).
  static constexpr std::size_t kMaxNumbersPerMessage = 20;
  /// Максимум записей на странице портфеля.
  static constexpr std::size_t kMaxPageSize = 100;

 private:
  struct UserRow {
    std::int64_t id{0};
    bool is_demo{true};
    DemoStage stage{DemoStage::kBase};
    bool consented{false};
  };

  drogon::Task<Result<UserRow>> ensure_user(std::int64_t max_user_id);
  /// Вердикты по сырым номерам + строки `check_log` одной пачки.
  drogon::Task<Result<CheckResult>> check_numbers(UserContext user, std::vector<std::string> raws,
                                                  std::chrono::steady_clock::time_point started,
                                                  bool from_file);
  /// `attach` — поставить с поставщиком или записать поставщика в уже наблюдаемый документ без SKU.
  drogon::Task<Result<AddResult>> add_for_user(UserRow user, AddRequest request, bool attach);
  [[nodiscard]] snapshot::SnapshotPtr snapshot_of(const UserRow& user) const {
    return snapshots_.for_user(user.is_demo, user.stage);
  }

  drogon::orm::DbClientPtr db_;
  const SnapshotSet& snapshots_;
  RecognitionPool& recognition_;
  RateLimiter& limiter_;
  NotifyService& notify_;
  TodayFn today_;
};

}  // namespace sk::certd
