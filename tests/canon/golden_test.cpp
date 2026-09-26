#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::canon {
namespace {

struct GoldenRow {
  int line{0};
  std::string input{};
  std::string canonical{};
  std::string serial{};
  std::string year{};
};

std::vector<GoldenRow> load_golden() {
  std::ifstream in{SK_CANON_GOLDEN};
  std::vector<GoldenRow> rows;
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ++n;
    if (line.empty() || line.starts_with('#')) {
      continue;
    }
    std::istringstream fields{line};
    GoldenRow r{.line = n};
    std::getline(fields, r.input, '\t');
    std::getline(fields, r.canonical, '\t');
    std::getline(fields, r.serial, '\t');
    std::getline(fields, r.year, '\t');
    rows.push_back(r);
  }
  return rows;
}

TEST(CanonGolden, FileHasArchitectureRowsFirst) {
  const auto rows = load_golden();
  ASSERT_GE(rows.size(), 10U);
  EXPECT_EQ(rows[0].input, "ЕАЭС N RU ДCR.РА08.В.89369/26");
  EXPECT_EQ(rows[1].input, "еаэс n ru дcr.ра08.в.89369/26");
  EXPECT_EQ(rows[2].input, "ЕАЭС М RU ДСВ.РАО8.В.89369/26");
}

TEST(CanonGolden, AllRows) {
  for (const auto& r : load_golden()) {
    SCOPED_TRACE("canon_golden.tsv:" + std::to_string(r.line) + " «" + r.input + "»");
    const auto c = canonicalize(r.input);
    if (r.canonical == "-") {
      EXPECT_FALSE(c.has_value()) << c.value_or("");
      continue;
    }
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c.value_or(""), r.canonical);
    // Канонизация идемпотентна.
    EXPECT_EQ(canonicalize(r.canonical).value_or(""), r.canonical);

    const auto n = parse(r.canonical);
    if (r.serial == "-") {
      EXPECT_FALSE(n.has_value());
      continue;
    }
    ASSERT_TRUE(n.has_value());
    const auto& num = n.value();  // NOLINT(bugprone-unchecked-optional-access): проверено ASSERT выше.
    EXPECT_EQ(num.canonical, r.canonical);
    EXPECT_EQ(num.serial, r.serial);
    EXPECT_EQ(std::to_string(num.year), std::to_string(std::stoi(r.year)));
  }
}

}  // namespace
}  // namespace sk::canon
