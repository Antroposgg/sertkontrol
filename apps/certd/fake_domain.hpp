/// @file fake_domain.hpp
/// @brief `FakeDomainService` — C6 в памяти на `FakeSnapshot` и fake-функциях C1/C4/C5.
///
/// Нужен боту и REST до готовности `DomainServiceImpl` (этап 1) и остаётся тестовым
/// дублем для тестов бота. Потокобезопасен (один мьютекс), данные теряются при рестарте.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
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
  drogon::Task<Result<std::vector<verify::Verdict>>> check_text(UserContext user, std::string text) override;
  drogon::Task<Result<std::vector<verify::Verdict>>> check_file(UserContext user, FileUpload file) override;
  drogon::Task<Result<Page<PortfolioItem>>> list_portfolio(UserContext user, PortfolioFilter filter) override;
  drogon::Task<Result<AddResult>> add_to_portfolio(UserContext user, AddRequest request) override;
  drogon::Task<Result<Ok>> remove_from_portfolio(UserContext user, std::int64_t item_id) override;
  drogon::Task<Result<DataStatus>> data_status(UserContext user) override;
  drogon::Task<Result<Ok>> simulate_update(UserContext user) override;
  drogon::Task<Result<Ok>> reset_demo(UserContext user) override;

  /// Сколько номеров максимум проверяется из одного текста (F1).
  static constexpr std::size_t kMaxNumbersPerMessage = 20;

 private:
  struct StoredItem {
    std::int64_t owner{0};
    PortfolioItem item;
  };

  [[nodiscard]] snapshot::SnapshotPtr snapshot_for(std::int64_t max_user_id) const;
  [[nodiscard]] std::vector<verify::Verdict> check_all(std::int64_t max_user_id,
                                                       const std::vector<std::string>& raws) const;

  snapshot::SnapshotPtr base_;
  snapshot::SnapshotPtr updated_;
  Date today_;

  mutable std::mutex mutex_;
  std::map<std::int64_t, DemoStage> stages_;  // max_user_id → стадия
  std::map<std::int64_t, StoredItem> items_;  // id → запись
  std::int64_t next_id_{1};
};

}  // namespace sk::certd
