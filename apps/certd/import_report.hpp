/// @file import_report.hpp
/// @brief Сборка `ImportReport` из результатов постановки строк импорта (F9) — общая для домена и его фейка.
#pragma once

#include <optional>

#include "domain.hpp"

namespace sk::certd {

/// Учитывает результат постановки строки `row` в отчёте. Возвращает ошибку, если импорт надо прервать
/// (сбой БД или данных — не ошибка строки); ошибки строки (`kConflict`, нераспознанный номер, неверный ИНН) —
/// в отчёт.
[[nodiscard]] std::optional<Error> account_import_row(ImportReport& report, const ImportRow& row,
                                                      const Result<AddResult>& added);

/// Запрос постановки для строки импорта: пустые SKU и ИНН — не указаны.
[[nodiscard]] AddRequest import_request(const ImportRow& row);

/// Строки с ошибками — по возрастанию номера строки: разбор и постановка добавляют их в разное время.
void finish_import_report(ImportReport& report);

}  // namespace sk::certd
