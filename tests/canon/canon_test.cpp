#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::canon {
namespace {

TEST(Canon, VersionIsOne) {
  EXPECT_EQ(kVersion, 1U);
}

TEST(Canon, KindFromLetter) {
  EXPECT_EQ(parse("RUD-RU.PA01.B.12345/23").value_or(Number{}).kind, DocKind::kDeclaration);
  EXPECT_EQ(parse("RUC-RU.PA01.B.12345/23").value_or(Number{}).kind, DocKind::kCertificate);
}

TEST(Canon, DigitsAreNeverChanged) {
  // O и 0 различает нечёткий поиск, а не канонизация (АРХ §7.1).
  EXPECT_EQ(canonicalize("RU D-RU.PAO1.B.12345/23"), "RUD-RU.PAO1.B.12345/23");
  EXPECT_EQ(canonicalize("RU D-RU.PA01.B.12345/23"), "RUD-RU.PA01.B.12345/23");
  EXPECT_NE(key_hash("RUD-RU.PAO1.B.12345/23"), key_hash("RUD-RU.PA01.B.12345/23"));
}

TEST(Canon, KeyHashIsXxh3Stable) {
  // XXH3_64bits(seed 0) пустой строки — константа из спецификации xxHash; ключи пишутся в файл.
  EXPECT_EQ(key_hash(""), 0x2D06800538D394C2ULL);
  EXPECT_EQ(key_hash("RUD-CR.PA08.B.89369/26"), key_hash("RUD-CR.PA08.B.89369/26"));
}

TEST(Canon, ParseRejectsGarbage) {
  for (const char* bad : {"", "RUD-", "RUX-A.1/23", "RUD-A.B.1/2", "RUD-A.B.1/234", "RUD-A.B.X/23", "RUD-/23",
                          "RUD-.1/23", "RUD-A B.1/23", "RUD-A.B.12345678901/23", "RUD-A.B./23"}) {
    EXPECT_FALSE(parse(bad).has_value()) << bad;
  }
}

TEST(Canon, InvalidUtf8DoesNotCrash) {
  const std::string bad = "RU D-RU.PA01.B.1\xff\xfe/23";
  EXPECT_TRUE(canonicalize(bad).has_value());
  EXPECT_FALSE(parse(canonicalize(bad).value_or("")).has_value());
  EXPECT_TRUE(find_numbers("\xc3\x28 RU D-A.B.1/23 \xe2\x82", 20).size() == 1);
}

TEST(FindNumbers, SplitsListsAndKeepsRawText) {
  const auto found = find_numbers(
      "Проверьте: ЕАЭС N RU Д-RU.РА01.В.12345/23, RU C-RU.АЯ46.В.00017/24;\n"
      "и ещё ru d-cn.pa02.b.54321/25.",
      20);
  ASSERT_EQ(found.size(), 3U);
  EXPECT_EQ(found[0], "RU Д-RU.РА01.В.12345/23");
  EXPECT_EQ(found[1], "RU C-RU.АЯ46.В.00017/24");
  EXPECT_EQ(found[2], "ru d-cn.pa02.b.54321/25");
}

TEST(FindNumbers, DeduplicatesByCanonicalForm) {
  const auto found = find_numbers("RU D-RU.PA01.B.12345/23 и ru д-ru.ра01.в.12345/23", 20);
  EXPECT_EQ(found, (std::vector<std::string>{"RU D-RU.PA01.B.12345/23"}));
}

TEST(FindNumbers, LimitsCount) {
  std::string text;
  for (int i = 0; i < 25; ++i) {
    text += "RU D-RU.PA01.B." + std::to_string(10000 + i) + "/23\n";
  }
  EXPECT_EQ(find_numbers(text, 20).size(), 20U);
  EXPECT_TRUE(find_numbers(text, 0).empty());
}

TEST(FindNumbers, NoNumbers) {
  EXPECT_TRUE(find_numbers("", 20).empty());
  EXPECT_TRUE(find_numbers("Здравствуйте! RUB 100, RU-сегмент", 20).empty());
  EXPECT_TRUE(find_numbers("RU D", 20).empty());
  EXPECT_TRUE(find_numbers("RU D- ,", 20).empty());
}

TEST(FindNumbers, WordBoundary) {
  // «RU» внутри латинского слова не начинает номер.
  EXPECT_TRUE(find_numbers("GURU D-A.B.1/23", 20).empty());
}

}  // namespace
}  // namespace sk::canon
