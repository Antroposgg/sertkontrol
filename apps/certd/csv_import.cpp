#include "csv_import.hpp"

#include <algorithm>
#include <utility>

#include "sertkontrol/verify/inn.hpp"

namespace sk::certd {

namespace {

struct CsvRecord {
  std::size_t line{0};
  std::vector<std::string> fields{};
};

std::string_view trim_field(std::string_view s) {
  const auto b = s.find_first_not_of(" \t");
  if (b == std::string_view::npos) {
    return {};
  }
  return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

/// Разделитель — по первой непустой строке: `;`, если он там есть вне кавычек, иначе `,`.
char detect_separator(std::string_view text) {
  bool quoted = false;
  bool seen_content = false;
  for (const char c : text) {
    if (c == '"') {
      quoted = !quoted;
    } else if (!quoted && c == ';') {
      return ';';
    } else if (!quoted && c == '\n' && seen_content) {
      break;
    }
    seen_content = seen_content || (c != '\n' && c != '\r' && c != ' ' && c != '\t');
  }
  return ',';
}

/// Записи CSV по RFC 4180 с номером строки начала записи. `false` — записей больше `limit`.
bool split_records(std::string_view text, char sep, std::size_t limit, std::vector<CsvRecord>& out) {
  CsvRecord rec{.line = 1};
  std::string field;
  bool quoted = false;
  std::size_t line = 1;
  const auto finish_record = [&] {
    rec.fields.push_back(std::move(field));
    field.clear();
    const bool blank =
        std::ranges::all_of(rec.fields, [](const std::string& f) { return trim_field(f).empty(); });
    if (!blank) {
      out.push_back(std::move(rec));
    }
    rec = CsvRecord{.line = line};
    return out.size() <= limit;
  };
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (quoted) {
      if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
        field += '"';
        ++i;
      } else if (c == '"') {
        quoted = false;
      } else {
        line += c == '\n' ? 1U : 0U;
        field += c;
      }
    } else if (c == '"' && trim_field(field).empty()) {
      field.clear();  // пробелы перед открывающей кавычкой не входят в значение
      quoted = true;
    } else if (c == sep) {
      rec.fields.push_back(std::move(field));
      field.clear();
    } else if (c == '\n') {
      ++line;
      if (!finish_record()) {
        return false;
      }
    } else if (c != '\r') {
      field += c;
    }
  }
  // Последняя запись без перевода строки (или с незакрытой кавычкой — берём как есть).
  if (!field.empty() || !rec.fields.empty()) {
    return finish_record();
  }
  return true;
}

bool has_digit(std::string_view s) {
  return std::ranges::any_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

Result<ParsedImport> parse_import_csv(std::string_view text) {
  if (text.size() > kMaxImportBytes) {
    return Error{ErrorCode::kFileTooLarge, "файл импорта больше 1 МБ"};
  }
  if (text.starts_with("\xEF\xBB\xBF")) {
    text.remove_prefix(3);
  }
  std::vector<CsvRecord> records;
  // +1 — на возможный заголовок.
  if (!split_records(text, detect_separator(text), kMaxImportRows + 1, records)) {
    return Error{ErrorCode::kFileTooLarge, "в файле импорта больше 1000 строк"};
  }
  ParsedImport out;
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& f = records[i].fields;
    const bool one_column = f.size() == 1;
    const auto number = trim_field(one_column ? f[0] : f[1]);
    if (i == 0 && !has_digit(number)) {
      continue;  // заголовок: «SKU;Номер;ИНН»
    }
    const auto sku = one_column ? std::string_view{} : trim_field(f[0]);
    const auto inn = f.size() >= 3 ? trim_field(f[2]) : std::string_view{};
    const auto line = records[i].line;
    if (number.empty()) {
      out.invalid.push_back({.line = line, .reason = "нет номера документа"});
    } else if (sku.size() > kMaxSkuLength) {
      out.invalid.push_back({.line = line, .reason = "SKU длиннее 100 символов"});
    } else if (!inn.empty() && !verify::inn_valid(inn)) {
      out.invalid.push_back(
          {.line = line, .reason = "ИНН поставщика: неверный формат или контрольная цифра"});
    } else {
      out.rows.push_back({.line = line,
                          .sku = std::string{sku},
                          .number = std::string{number},
                          .supplier_inn = std::string{inn}});
    }
  }
  if (out.rows.size() + out.invalid.size() > kMaxImportRows) {
    return Error{ErrorCode::kFileTooLarge, "в файле импорта больше 1000 строк"};
  }
  if (out.rows.empty() && out.invalid.empty()) {
    return Error{ErrorCode::kNumberNotRecognized, "в файле нет строк с номерами документов"};
  }
  return out;
}

}  // namespace sk::certd
