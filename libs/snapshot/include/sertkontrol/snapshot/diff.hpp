/// @file diff.hpp
/// @brief Diff двух снапшотов за O(N_old + N_new) — поток B, шаг 2 (АРХ §4, §7.3). Владелец R1.
///
/// Оба снапшота отсортированы по одному полному порядку `(key_hash, каноническая строка)` (writer,
/// АРХ §7.3), поэтому merge-join посещает каждую запись ровно один раз и читает оба массива последовательно.
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "sertkontrol_contracts.hpp"

namespace sk::snapshot {

/// Наблюдаемое состояние документа: то, что сравнивает diff (АРХ §7.3).
struct DocState {
  Status status{Status::kUnknown};
  std::optional<Date> expiry_date{};
  std::optional<Date> status_date{};

  friend bool operator==(const DocState&, const DocState&) = default;
};

/// Изменение одного документа между снапшотами. `before` пуст — документ появился, `after` пуст — исчез.
struct DocChange {
  std::string doc_key{};  ///< Каноническая форма номера.
  std::optional<DocState> before{};
  std::optional<DocState> after{};
};

/// Агрегаты diff — пишутся в `snapshot_version.stats` (АРХ §4, поток B, шаг 2).
struct DiffStats {
  std::size_t added{0};
  std::size_t removed{0};
  std::size_t changed{0};
  std::size_t unchanged{0};
  /// Переходы статусов `(было, стало)` → число документов; только для совпавших ключей.
  std::map<std::pair<Status, Status>, std::size_t> transitions{};
};

/// Состояние записи снапшота.
[[nodiscard]] DocState state_of(const RecordView& record);

/// Сравнивает снапшоты и вызывает `sink` для каждого появившегося, исчезнувшего или изменившегося документа
/// в порядке `(key_hash, строка)`. Одинаковые документы в `sink` не попадают, но учитываются в `unchanged`.
/// `sink` может быть пустым — тогда считаются только агрегаты.
DiffStats diff(const Snapshot& before, const Snapshot& after,
               const std::function<void(const DocChange&)>& sink);

}  // namespace sk::snapshot
