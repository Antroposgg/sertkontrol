/// @file normalize.hpp
/// @brief Нормализация записей источника (C3 `RawRecord`) в записи снапшота (`snapshot::RecordInput`).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sertkontrol/snapshot/writer.hpp"
#include "sertkontrol_contracts.hpp"
#include "source_adapter.hpp"

namespace sk::ingest {

/// Порог качества (АРХ §4 «Отказы потока B»): отвергнуто больше 5% записей — сборка прерывается.
inline constexpr double kMaxRejectedShare = 0.05;

/// Статус источника → `snapshot::Status`. Регистр и «ё/е» не важны; неизвестное → `nullopt`.
[[nodiscard]] std::optional<snapshot::Status> status_from_source(std::string_view text);

/// Дата ISO 8601 `YYYY-MM-DD`. Пустая строка → «нет даты» (`std::nullopt` во втором поле `ok = true`).
struct ParsedDate {
  bool ok{false};
  std::optional<Date> date{};
};
[[nodiscard]] ParsedDate parse_iso_date(std::string_view text);

/// Итог нормализации набора.
struct Normalized {
  std::vector<snapshot::RecordInput> records{};
  std::size_t total{0};
  std::vector<std::string> rejected{};  ///< Причины отказа по записям, «номер: причина».
};

/// Нормализует одну запись; при ошибке возвращает причину.
[[nodiscard]] Result<snapshot::RecordInput> normalize(const RawRecord& raw);

/// Нормализует поток записей адаптера. Ошибка — если адаптер упал или доля отказов > `kMaxRejectedShare`.
template <SourceAdapter A>
[[nodiscard]] Result<Normalized> normalize_all(A& adapter) {
  Normalized out;
  const auto streamed = adapter.for_each([&](RawRecord&& incoming) {
    const RawRecord raw = std::move(incoming);
    ++out.total;
    auto rec = normalize(raw);
    if (rec) {
      out.records.push_back(std::move(rec).value());
    } else {
      out.rejected.push_back(raw.number + ": " + rec.error().detail);
    }
  });
  if (!streamed) {
    return streamed.error();
  }
  if (out.total > 0 &&
      static_cast<double>(out.rejected.size()) > kMaxRejectedShare * static_cast<double>(out.total)) {
    return Error{ErrorCode::kInvalidArgument, "quality gate: отвергнуто " +
                                                  std::to_string(out.rejected.size()) + " из " +
                                                  std::to_string(out.total) + " записей"};
  }
  return out;
}

}  // namespace sk::ingest
