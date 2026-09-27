/// @file diff.cpp
/// @brief Merge-join двух снапшотов (АРХ §7.3).
///
/// Корректность: оба массива строго возрастают по ключу сравнения `(keys[i], record(i).number)` — writer
/// сортирует по этому порядку и убирает повторы номера. Инвариант цикла: все записи левее `i` в `before` и
/// левее `j` в `after` уже классифицированы. Меньший из текущих элементов не может встретиться в другом
/// массиве правее текущей позиции (строгое возрастание), поэтому он «исчез» или «появился»; равные
/// сравниваются по `DocState`. Каждый шаг сдвигает `i` или `j`, поэтому шагов не больше N_old + N_new.
///
/// Строки сравниваются только при равных хэшах: на различающихся ключах запись не распаковывается,
/// и diff читает плотный массив `keys` последовательно.
#include "sertkontrol/snapshot/diff.hpp"

#include <string_view>

namespace sk::snapshot {

DocState state_of(const RecordView& record) {
  return DocState{
      .status = record.status, .expiry_date = record.expiry_date, .status_date = record.status_date};
}

DiffStats diff(const Snapshot& before, const Snapshot& after,
               const std::function<void(const DocChange&)>& sink) {
  DiffStats stats;
  const auto kb = before.keys();
  const auto ka = after.keys();
  const std::size_t n = kb.size();
  const std::size_t m = ka.size();
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < n || j < m) {
    // cmp < 0 — текущая запись `before` меньше (исчезла), > 0 — меньше запись `after` (появилась).
    int cmp = 0;
    if (j == m) {
      cmp = -1;
    } else if (i == n) {
      cmp = 1;
    } else if (kb[i] != ka[j]) {
      cmp = kb[i] < ka[j] ? -1 : 1;
    } else {
      const auto c = before.record(i).number.compare(after.record(j).number);
      cmp = c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    if (cmp < 0) {
      const auto rec = before.record(i++);
      ++stats.removed;
      if (sink) {
        sink(DocChange{.doc_key = std::string{rec.number}, .before = state_of(rec), .after = std::nullopt});
      }
      continue;
    }
    if (cmp > 0) {
      const auto rec = after.record(j++);
      ++stats.added;
      if (sink) {
        sink(DocChange{.doc_key = std::string{rec.number}, .before = std::nullopt, .after = state_of(rec)});
      }
      continue;
    }
    const auto s0 = state_of(before.record(i++));
    const auto new_rec = after.record(j++);
    const auto s1 = state_of(new_rec);
    if (s0.status != s1.status) {
      ++stats.transitions[{s0.status, s1.status}];
    }
    if (s0 == s1) {
      ++stats.unchanged;
      continue;
    }
    ++stats.changed;
    if (sink) {
      sink(DocChange{.doc_key = std::string{new_rec.number}, .before = s0, .after = s1});
    }
  }
  return stats;
}

}  // namespace sk::snapshot
