#include "csv_import.hpp"

#include <gtest/gtest.h>

#include <string>

namespace sk::certd {
namespace {

ParsedImport parsed(std::string_view text) {
  auto r = parse_import_csv(text);
  if (!r) {
    ADD_FAILURE() << r.error().detail;
    return {};
  }
  return std::move(r).value();
}

// Excel в русской локали: `;`, BOM, CRLF, заголовок.
TEST(CsvImport, ExcelSemicolonWithHeaderAndBom) {
  const auto p = parsed(
      "\xEF\xBB\xBFSKU;Номер;ИНН поставщика\r\n"
      "ЧАЙ-01;ЕАЭС N RU Д-CR.РА08.В.89369/26;7700000016\r\n"
      "ЧАЙ-02; RU D-RU.PA01.B.10001/25 ;\r\n");
  ASSERT_EQ(p.rows.size(), 2U);
  EXPECT_TRUE(p.invalid.empty());
  EXPECT_EQ(p.rows[0].line, 2U);
  EXPECT_EQ(p.rows[0].sku, "ЧАЙ-01");
  EXPECT_EQ(p.rows[0].number, "ЕАЭС N RU Д-CR.РА08.В.89369/26");
  EXPECT_EQ(p.rows[0].supplier_inn, "7700000016");
  EXPECT_EQ(p.rows[1].line, 3U);
  EXPECT_EQ(p.rows[1].number, "RU D-RU.PA01.B.10001/25");
  EXPECT_TRUE(p.rows[1].supplier_inn.empty());
}

// Запятая, кавычки RFC 4180 (разделитель и `""` внутри, перевод строки в поле), одна колонка — только номер.
TEST(CsvImport, CommaAndQuotes) {
  const auto p = parsed(
      "\"Чайник, белый\",RU D-CR.PA08.B.89369/26,7700000016\n"
      "\"SKU \"\"7\"\"\",\"RU D-RU.PA01.B.10001/25\"\n"
      "\"две\nстроки\",RU C-RU.AЯ46.B.10005/24\n"
      "\n"
      "RU D-RU.PA01.B.10009/25\n");
  ASSERT_EQ(p.rows.size(), 4U);
  EXPECT_EQ(p.rows[0].sku, "Чайник, белый");
  EXPECT_EQ(p.rows[1].sku, "SKU \"7\"");
  EXPECT_EQ(p.rows[1].number, "RU D-RU.PA01.B.10001/25");
  EXPECT_EQ(p.rows[2].sku, "две\nстроки");
  EXPECT_EQ(p.rows[2].line, 3U);
  EXPECT_EQ(p.rows[3].line, 6U);  // поле в две строки и пустая строка сдвигают нумерацию
  EXPECT_EQ(p.rows[3].number, "RU D-RU.PA01.B.10009/25");
  EXPECT_TRUE(p.rows[3].sku.empty());
}

TEST(CsvImport, InvalidRowsGoToReport) {
  const auto p = parsed(
      "SKU;Номер;ИНН\n"
      "A;;7700000016\n"
      "B;RU D-CR.PA08.B.89369/26;7700000017\n" +
      std::string(101, 'x') + ";RU D-CR.PA08.B.89369/26\nC;RU D-CR.PA08.B.89369/26;7700000016\n");
  ASSERT_EQ(p.rows.size(), 1U);
  EXPECT_EQ(p.rows[0].line, 5U);
  ASSERT_EQ(p.invalid.size(), 3U);
  EXPECT_EQ(p.invalid[0].line, 2U);
  EXPECT_EQ(p.invalid[0].reason, "нет номера документа");
  EXPECT_EQ(p.invalid[1].reason, "ИНН поставщика: неверный формат или контрольная цифра");
  EXPECT_EQ(p.invalid[2].reason, "SKU длиннее 100 символов");
}

TEST(CsvImport, FileErrors) {
  EXPECT_EQ(parse_import_csv("").error().code, ErrorCode::kNumberNotRecognized);
  EXPECT_EQ(parse_import_csv("SKU;Номер;ИНН\n\n").error().code, ErrorCode::kNumberNotRecognized);
  EXPECT_EQ(parse_import_csv(std::string(kMaxImportBytes + 1, 'a')).error().code, ErrorCode::kFileTooLarge);
  std::string many;
  for (std::size_t i = 0; i <= kMaxImportRows; ++i) {
    many += "RU D-CR.PA08.B.89369/26\n";
  }
  EXPECT_EQ(parse_import_csv(many).error().code, ErrorCode::kFileTooLarge);
  many.resize(many.size() - std::string{"RU D-CR.PA08.B.89369/26\n"}.size());
  EXPECT_EQ(parsed(many).rows.size(), kMaxImportRows);  // ровно лимит — можно
  // Незакрытая кавычка в конце файла — значение берётся как есть, без падения.
  EXPECT_EQ(parsed("A;\"RU D-CR.PA08.B.89369/26").rows.at(0).number, "RU D-CR.PA08.B.89369/26");
}

}  // namespace
}  // namespace sk::certd
