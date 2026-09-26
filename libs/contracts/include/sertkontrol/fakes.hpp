/// @file fakes.hpp
/// @brief Fake-реализации контрактов C1, C2, C4, C5 — чтобы потребители не ждали поставщиков (АРХ §5).
///
/// ВНИМАНИЕ: это упрощённые тестовые дубли этапа 0, а не реализация правил АРХ §7.
/// Они живут в отдельном пространстве имён `sk::fake`, чтобы не конфликтовать
/// с настоящими `sk::canon`, `sk::verify`, `sk::recog`, и заменяются на этапе 1
/// (см. docs/plan.md, «Заглушки этапа 0»). `FakeSnapshot` остаётся тестовым дублем и дальше.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::fake {

// ─────────────── C1 (fake) ───────────────

/// Упрощённая канонизация: ASCII в верхний регистр, без пробелов, отброшено всё до `RUD-` / `RUC-`.
/// Кириллические гомоглифы НЕ заменяются (это делает настоящий `canon`, этап 1).
[[nodiscard]] std::optional<std::string> canonicalize(std::string_view raw);

/// Разбор `…<serial>/<yy>`: цифры между последней точкой и `/`, две цифры года.
[[nodiscard]] std::optional<canon::Number> parse(std::string_view canonical);

/// FNV-1a 64 бита. НЕ совместим с XXH3 настоящего `canon::key_hash`.
[[nodiscard]] std::uint64_t key_hash(std::string_view canonical) noexcept;

/// Режет текст по переводам строк, запятым и точкам с запятой; оставляет фрагменты с `RU`.
[[nodiscard]] std::vector<std::string> find_numbers(std::string_view text, std::size_t max_count);

// ─────────────── C2 (fake) ───────────────

/// Владеющая запись для построения `FakeSnapshot`.
struct FakeRecord {
  std::string number{};  ///< Каноническая форма (в терминах fake-канонизации).
  snapshot::Status status{snapshot::Status::kActive};
  std::optional<Date> issue_date{};
  std::optional<Date> expiry_date{};
  std::optional<Date> status_date{};
  std::string applicant_name{};
  std::string applicant_inn{};
  std::string manufacturer_name{};
  std::string product{};
  std::string tnved{};
  std::uint64_t registry_id{0};
};

/// Снапшот в памяти. Записи сортируются по `fake::key_hash`, как в настоящем формате.
class FakeSnapshot final : public snapshot::Snapshot {
 public:
  /// @param records записи в любом порядке.
  /// @param meta метаданные снапшота.
  FakeSnapshot(std::vector<FakeRecord> records, snapshot::SnapshotMeta meta);

  /// Три записи по умолчанию: действующая, прекращённая, истёкшая.
  [[nodiscard]] static std::shared_ptr<const FakeSnapshot> three_records();

  [[nodiscard]] const snapshot::SnapshotMeta& meta() const noexcept override { return meta_; }
  [[nodiscard]] std::size_t size() const noexcept override { return records_.size(); }
  [[nodiscard]] std::span<const std::uint64_t> keys() const noexcept override { return keys_; }
  [[nodiscard]] snapshot::RecordView record(std::size_t index) const override;
  [[nodiscard]] std::vector<std::uint32_t> by_serial(std::string_view serial,
                                                     std::uint8_t year) const override;

 private:
  std::vector<FakeRecord> records_{};
  std::vector<std::uint64_t> keys_{};
  snapshot::SnapshotMeta meta_{};
};

// ─────────────── C4 (fake) ───────────────

/// Только точное совпадение и одно правило статуса + срока. Метки «факт / расчёт» проставлены.
[[nodiscard]] verify::Verdict check(const snapshot::Snapshot& snap, const verify::Query& query);

// ─────────────── C5 (fake) ───────────────

/// Считает байты файла текстовым слоем и ищет в них номера через `fake::find_numbers`.
/// Проверяет лимит `max_bytes`.
[[nodiscard]] Result<std::vector<recog::Found>> recognize(std::span<const std::byte> file,
                                                          recog::MediaType type, const recog::Limits& limits);

// ─────────────── Утилиты ───────────────

/// Дата в формате `ДД.ММ.ГГГГ` для текстов карточки.
[[nodiscard]] std::string format_date(Date date);

}  // namespace sk::fake
