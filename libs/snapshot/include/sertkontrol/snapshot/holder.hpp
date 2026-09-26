/// @file holder.hpp
/// @brief `SnapshotHolder` — текущий снапшот процесса с атомарной заменой (АРХ §7.4).
#pragma once

#include <atomic>
#include <memory>
#include <utility>

#include "sertkontrol_contracts.hpp"

namespace sk::snapshot {

/// Держит текущий снапшот. Обработчик берёт `get()` один раз на запрос и держит указатель
/// до конца запроса; новый снапшот виден следующим запросам, старый освобождается
/// (`munmap`) вместе с последним читателем.
class SnapshotHolder {
 public:
  /// Текущий снапшот или `nullptr`, если ещё не загружен.
  [[nodiscard]] SnapshotPtr get() const noexcept { return ptr_.load(std::memory_order_acquire); }

  /// Заменяет снапшот.
  void set(SnapshotPtr next) noexcept { ptr_.store(std::move(next), std::memory_order_release); }

 private:
  std::atomic<SnapshotPtr> ptr_{};
};

}  // namespace sk::snapshot
