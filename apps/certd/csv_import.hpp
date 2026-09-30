/// @file csv_import.hpp
/// @brief Разбор CSV импорта портфеля (F9): «SKU; номер; ИНН поставщика». Чистая функция без IO и Drogon — её
/// же гоняет fuzz-цель `fuzz_import_csv` (АРХ §10 «Fuzz»).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Лимиты импорта (docs/plan.md §8.2 п.6): приёмка F9 — 500 строк за 10 с, берём с двойным запасом.
inline constexpr std::size_t kMaxImportBytes = std::size_t{1024} * 1024;
inline constexpr std::size_t kMaxImportRows = 1000;
/// Длина SKU: артикул, а не описание товара.
inline constexpr std::size_t kMaxSkuLength = 100;

/// Строка файла, пригодная к постановке на контроль. `line` — номер строки файла с 1 (для отчёта).
struct ImportRow {
  std::size_t line{0};
  std::string sku{};
  std::string number{};
  std::string supplier_inn{};  ///< Пусто — поставщик не указан.
};

/// Строка, которую не удалось разобрать, и почему — человеческим языком.
struct ImportIssue {
  std::size_t line{0};
  std::string reason{};
};

/// Разобранный файл.
struct ParsedImport {
  std::vector<ImportRow> rows{};
  std::vector<ImportIssue> invalid{};
};

/// Разбирает CSV: колонки «SKU; номер; ИНН поставщика» (одна колонка — только номер; лишние — игнорируются);
/// разделитель `;` или `,` — по первой непустой строке (`;` важнее: так сохраняет Excel в русской локали);
/// поля в кавычках по RFC 4180 (`""` — кавычка, переводы строк внутри); BOM UTF-8 и пустые строки
/// пропускаются; первая строка без цифр в колонке номера — заголовок. ИНН проверяется контрольными цифрами.
/// Ошибки: `kFileTooLarge` — больше `kMaxImportBytes` или `kMaxImportRows` строк данных;
/// `kNumberNotRecognized` — в файле нет ни одной строки данных (пусто или только заголовок). Строки с
/// ошибками не делают файл ошибочным: они уходят в `invalid` и попадают в отчёт.
[[nodiscard]] Result<ParsedImport> parse_import_csv(std::string_view text);

}  // namespace sk::certd
