#include "sertkontrol/verify/fuzzy.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "sertkontrol/snapshot/writer.hpp"
#include "sertkontrol_contracts.hpp"
#include "support/pg.hpp"

namespace sk::verify {
namespace {

using snapshot::RecordInput;
using snapshot::Status;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr Date kToday = year{2026} / month{9} / day{26};

snapshot::SnapshotPtr build(std::vector<RecordInput> records, const std::string& name) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("sk-fuzzy-" + name + "-" + std::to_string(test::unique_id()) + ".bin");
  EXPECT_TRUE(snapshot::write_snapshot(path, std::move(records),
                                       {.version = 1, .source = "test", .source_date = kToday}));
  auto snap = snapshot::open_snapshot(path);
  std::filesystem::remove(path);  // отображение живёт, пока жив SnapshotPtr
  return std::move(snap).value();
}

TEST(WeightedDistance, Costs) {
  EXPECT_DOUBLE_EQ(weighted_distance("", ""), 0.0);
  EXPECT_DOUBLE_EQ(weighted_distance("ABC", ""), 3.0);
  EXPECT_DOUBLE_EQ(weighted_distance("A0", "AO"), 0.3);  // O↔0
  for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{
           {"0", "O"}, {"8", "B"}, {"5", "S"}, {"1", "I"}, {"2", "Z"}}) {
    EXPECT_DOUBLE_EQ(weighted_distance(a, b), 0.3) << a << b;
    EXPECT_DOUBLE_EQ(weighted_distance(b, a), 0.3) << b << a;
  }
  EXPECT_DOUBLE_EQ(weighted_distance("R", "B"), 1.0);
  EXPECT_DOUBLE_EQ(weighted_distance("AЯ46", "AБ46"), 1.0);  // кириллица — одна правка, не две
  EXPECT_DOUBLE_EQ(weighted_distance("ABCD", "ABD"), 1.0);
  // Пример АРХ §7.2: B→R (1) и O→0 (0,3).
  EXPECT_NEAR(weighted_distance("RUD-CB.PAO8.B.89369/26", "RUD-CR.PA08.B.89369/26"), 1.3, 1e-9);
  // Мусор не роняет разбор.
  EXPECT_GE(weighted_distance("\xff\xfe\xc3", "A"), 1.0);
  // Длинные строки обрезаются до 64 кодовых точек.
  EXPECT_DOUBLE_EQ(weighted_distance(std::string(200, 'A'), std::string(64, 'A')), 0.0);
}

class FuzzyTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    snap() = build({{.number = "RUD-CR.PA08.B.89369/26", .status = Status::kActive},
                    {.number = "RUD-CR.PA09.B.89369/26", .status = Status::kActive},
                    {.number = "RUD-RU.PA02.B.20000/25", .status = Status::kActive},
                    {.number = "RUD-RU.PA03.B.20000/25", .status = Status::kActive},
                    {.number = "RUD-RU.XX77.A.55555/24", .status = Status::kTerminated}},
                   "fixture");
  }
  static void TearDownTestSuite() { snap().reset(); }
  static snapshot::SnapshotPtr& snap() {
    static snapshot::SnapshotPtr v;
    return v;
  }
  static Verdict run(const std::string& text) { return check(*snap(), {.text = text, .today = kToday}); }
};

// АРХ §7.1, третья строка golden-файла: OCR прочитал номер с ошибками — точного совпадения нет, вопрос «Это
// номер …?».
TEST_F(FuzzyTest, OcrStringFromArchitecture) {
  const auto v = run("ЕАЭС М RU ДСВ.РАО8.В.89369/26");
  EXPECT_EQ(v.number, "RUD-CB.PAO8.B.89369/26");
  EXPECT_EQ(v.level, Level::kNeedsConfirmation);
  ASSERT_FALSE(v.suggestions.empty());
  EXPECT_EQ(v.suggestions[0].number, "RUD-CR.PA08.B.89369/26");
  EXPECT_NEAR(v.distance, 1.3, 1e-9);
  EXPECT_FALSE(v.card.has_value());
  std::vector<std::string> rules;
  rules.reserve(v.findings.size());
  for (const auto& f : v.findings) {
    rules.push_back(f.rule);
  }
  EXPECT_EQ(rules, (std::vector<std::string>{"not_found", "fuzzy.match", "advice.confirm_number"}));
}

TEST_F(FuzzyTest, DistortedSerialFoundByVariants) {
  const auto digit = run("RU D-CR.PA08.B.89368/26");  // одна цифра серии
  EXPECT_EQ(digit.level, Level::kNeedsConfirmation);
  EXPECT_EQ(digit.suggestions.at(0).number, "RUD-CR.PA08.B.89369/26");
  const auto letter = run("RU D-CR.PA08.B.8936O/26");  // буква O вместо цифры
  EXPECT_EQ(letter.level, Level::kNeedsConfirmation);
  EXPECT_EQ(letter.suggestions.at(0).number, "RUD-CR.PA08.B.89369/26");
  const auto year_letter = run("RU D-CR.PA08.B.89369/2S");  // S вместо 5 в годе — год 25, совпадений нет
  EXPECT_EQ(year_letter.level, Level::kNotFound);
}

TEST_F(FuzzyTest, AmbiguousOrFarIsNotConfident) {
  const auto ambiguous = run("RU D-RU.PA05.B.20000/25");  // PA02 и PA03 — оба на расстоянии 1
  EXPECT_EQ(ambiguous.level, Level::kNotFound);
  EXPECT_EQ(ambiguous.suggestions.size(), 2U);
  const auto far = run("RU D-QQ.ZZZZ.Q.55555/24");  // серия та же, остальное далеко
  EXPECT_EQ(far.level, Level::kNotFound);
  EXPECT_EQ(far.suggestions.size(), 1U);
  EXPECT_GT(far.suggestions[0].distance, kAcceptDistance);
  EXPECT_TRUE(run("RU D-A.B.1").suggestions.empty());  // без года — кандидатов нет
}

TEST_F(FuzzyTest, ExactMatchIsNotFuzzy) {
  const auto v = run("RU D-CR.PA08.B.89369/26");
  EXPECT_EQ(v.level, Level::kOk);
  EXPECT_DOUBLE_EQ(v.distance, 0.0);
}

// Полнота кандидатов против перебора (АРХ §10, «Property»): если лучший по полному перебору номер отличается
// от запроса не больше чем одной цифрой серии (с учётом букв-двойников) и с тем же годом, нечёткий поиск
// находит кандидата с тем же расстоянием.
TEST(FuzzyProperty, MatchesBruteForceOn10kRecords) {
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): фиксированное зерно — воспроизводимый property-тест.
  std::mt19937 rng{20260929};
  const std::string letters = "ABCEHKMOPTXYRU";
  const auto pick = [&](const std::string& alphabet) {
    return alphabet[std::uniform_int_distribution<std::size_t>{0, alphabet.size() - 1}(rng)];
  };
  const auto digits = [&](int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
      s.push_back(static_cast<char>('0' + std::uniform_int_distribution<int>{0, 9}(rng)));
    }
    return s;
  };
  constexpr int kRecords = 10'000;
  std::vector<RecordInput> records;
  std::vector<std::string> numbers;
  records.reserve(kRecords);
  numbers.reserve(kRecords);
  for (int i = 0; i < kRecords; ++i) {
    // Узкий диапазон серий, чтобы у номеров были «соседи» по серии.
    const auto serial = std::to_string(10'000 + std::uniform_int_distribution<int>{0, 1999}(rng));
    const auto yy = std::to_string(20 + std::uniform_int_distribution<int>{0, 6}(rng));
    std::string n = "RUD-";
    n += pick(letters);
    n += pick(letters);
    n.append(".PA").append(digits(2)).append(".B.").append(serial).append("/").append(yy);
    numbers.push_back(n);
    records.push_back({.number = n, .status = Status::kActive});
  }
  const auto snap = build(records, "property");
  const auto mutate = [&](std::string s) {
    const auto edits = std::uniform_int_distribution<int>{1, 3}(rng);
    for (int e = 0; e < edits; ++e) {
      const auto pos = std::uniform_int_distribution<std::size_t>{4, s.size() - 4}(rng);
      switch (std::uniform_int_distribution<int>{0, 2}(rng)) {
        case 0:
          s[pos] = pick("0123456789OBSIZ");
          break;
        case 1:
          s.erase(pos, 1);
          break;
        default:
          s.insert(pos, 1, pick(letters));
          break;
      }
    }
    return s;
  };
  const auto folded_serial_year = [](const std::string& s) -> std::string {
    const auto slash = s.rfind('/');
    const auto dot = s.rfind('.', slash);
    if (slash == std::string::npos || dot == std::string::npos) {
      return {};
    }
    std::string out = s.substr(dot + 1);
    static constexpr std::string_view kLetters = "OBSIZ";
    static constexpr std::string_view kDigits = "08512";
    for (auto& c : out) {
      if (const auto pos = kLetters.find(c); pos != std::string_view::npos) {
        c = kDigits[pos];
      }
    }
    return out;
  };
  const auto one_digit_apart = [](const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
      return false;
    }
    int diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
      diff += a[i] != b[i] ? 1 : 0;
    }
    return diff <= 1 && a.substr(a.size() - 3) == b.substr(b.size() - 3);
  };
  int checked = 0;
  for (int q = 0; q < 300; ++q) {
    const auto& origin = numbers[std::uniform_int_distribution<std::size_t>{0, numbers.size() - 1}(rng)];
    const auto query = mutate(origin);
    double brute = std::numeric_limits<double>::infinity();
    std::string brute_number;
    for (const auto& n : numbers) {
      if (n == query) {
        continue;
      }
      const auto d = weighted_distance(query, n);
      if (d < brute) {
        brute = d;
        brute_number = n;
      }
    }
    const auto fz = fuzzy_match(*snap, query);
    if (!fz.ranked.empty()) {
      EXPECT_GE(fz.ranked.front().distance + 1e-9, brute)
          << query;  // не лучше перебора — иначе перебор неверен
    }
    if (brute > kAcceptDistance ||
        !one_digit_apart(folded_serial_year(query), folded_serial_year(brute_number))) {
      continue;
    }
    ++checked;
    ASSERT_FALSE(fz.ranked.empty()) << query << " ~ " << brute_number;
    EXPECT_NEAR(fz.ranked.front().distance, brute, 1e-9) << query << " ~ " << brute_number;
  }
  EXPECT_GT(checked, 100);  // свойство действительно проверено на большой доле запросов
}

}  // namespace
}  // namespace sk::verify
