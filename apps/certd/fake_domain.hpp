/// @file fake_domain.hpp
/// @brief `FakeDomainService` — C6 в памяти на `FakeSnapshot` и fake-функциях C1/C4/C5.
///
/// Тестовый дубль для бота и REST: те же правила владения, конфликтов и демо-стадий, что у
/// `DomainServiceImpl`, но без PostgreSQL. Потокобезопасен (один мьютекс), данные живут в памяти.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "domain.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::certd {

class FakeDomainService final : public DomainService {
 public:
  /// @param base снапшот N; @param updated снапшот N+1 для демо (может совпадать с base).
  /// @param today «сегодня» для расчётных правил.
  FakeDomainService(snapshot::SnapshotPtr base, snapshot::SnapshotPtr updated, Date today);

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

 private:
  struct StoredItem {
    std::int64_t owner{0};
    PortfolioItem item{};
  };
  struct StoredCheck {
    std::int64_t owner{0};
    std::int64_t batch{0};
    std::string number{};
  };

  [[nodiscard]] snapshot::SnapshotPtr snapshot_for(std::int64_t max_user_id) const;
  [[nodiscard]] CheckResult check_all(std::int64_t max_user_id, const std::vector<std::string>& raws);
  [[nodiscard]] Result<AddResult> add_locked(std::int64_t owner, AddRequest request);
  [[nodiscard]] snapshot::SnapshotPtr snapshot_locked(std::int64_t max_user_id) const;

  snapshot::SnapshotPtr base_;
  snapshot::SnapshotPtr updated_;
  Date today_;

  mutable std::mutex mutex_;
  std::map<std::int64_t, DemoStage> stages_;    // max_user_id → стадия
  std::map<std::int64_t, bool> consents_;       // max_user_id → согласие
  std::map<std::int64_t, StoredItem> items_;    // id → запись
  std::map<std::int64_t, StoredCheck> checks_;  // check_id → проверка
  std::int64_t next_id_{1};
};

}  // namespace sk::certd
