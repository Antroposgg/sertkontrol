/// @file snapshot_set.hpp
/// @brief Снапшоты, открытые в `certd`: боевой, демо N и демо N+1 (АРХ §4 «Демо-вариант потока B», §7.4).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "domain.hpp"
#include "sertkontrol/snapshot/holder.hpp"

namespace sk::certd {

/// Роль версии в `snapshot_version` (C8): `is_demo = false` — боевая, иначе по `demo_stage`.
enum class SnapshotRole : std::uint8_t {
  kProd = 0,
  kDemoBase = 1,     ///< Демо N (`demo_stage = 0`).
  kDemoUpdated = 2,  ///< Демо N+1 (`demo_stage = 1`).
};
inline constexpr std::size_t kSnapshotRoles = 3;

[[nodiscard]] constexpr std::string_view to_string(SnapshotRole role) noexcept {
  switch (role) {
    case SnapshotRole::kProd:
      return "prod";
    case SnapshotRole::kDemoBase:
      return "demo-N";
    case SnapshotRole::kDemoUpdated:
      return "demo-N+1";
  }
  return "prod";
}

/// Три держателя с атомарной заменой каждого. Обработчик берёт снапшот один раз на запрос (АРХ §7.4).
class SnapshotSet {
 public:
  [[nodiscard]] snapshot::SnapshotPtr get(SnapshotRole role) const noexcept {
    return holders_.at(static_cast<std::size_t>(role)).get();
  }
  void set(SnapshotRole role, snapshot::SnapshotPtr snap) noexcept {
    holders_.at(static_cast<std::size_t>(role)).set(std::move(snap));
  }

  /// Роль, которую видит пользователь: демо — снапшот своей стадии, остальные — боевой.
  [[nodiscard]] static constexpr SnapshotRole role_for(bool is_demo, DemoStage stage) noexcept {
    if (!is_demo) {
      return SnapshotRole::kProd;
    }
    return stage == DemoStage::kUpdated ? SnapshotRole::kDemoUpdated : SnapshotRole::kDemoBase;
  }

  /// Снапшот пользователя. Демо-стадии без загруженного N+1 откатываются к N; боевые данные демо не
  /// подменяются — тестовые записи не должны выглядеть официальными (КЕЙС §2 п.10).
  [[nodiscard]] snapshot::SnapshotPtr for_user(bool is_demo, DemoStage stage) const noexcept {
    const auto role = role_for(is_demo, stage);
    auto snap = get(role);
    if (!snap && role == SnapshotRole::kDemoUpdated) {
      snap = get(SnapshotRole::kDemoBase);
    }
    return snap;
  }

  /// Для `/healthz`: боевой снапшот, если он есть, иначе демо N.
  [[nodiscard]] snapshot::SnapshotPtr primary() const noexcept {
    auto snap = get(SnapshotRole::kProd);
    return snap ? snap : get(SnapshotRole::kDemoBase);
  }

 private:
  std::array<snapshot::SnapshotHolder, kSnapshotRoles> holders_{};
};

}  // namespace sk::certd
