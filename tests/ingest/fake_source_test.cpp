#include "fake_source.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace sk::ingest {
namespace {

TEST(FakeSource, StreamsAllRecords) {
  using std::chrono::day;
  using std::chrono::month;
  using std::chrono::year;
  const Date date = year{2026} / month{9} / day{26};
  FakeSource src{{{.number = "A"}, {.number = "B"}}, date};
  std::vector<std::string> seen;
  const auto n = src.for_each([&](RawRecord&& r) {
    RawRecord rec = std::move(r);
    seen.push_back(std::move(rec.number));
  });
  ASSERT_TRUE(n);
  EXPECT_EQ(n.value(), 2U);
  EXPECT_EQ(seen, (std::vector<std::string>{"A", "B"}));
  EXPECT_EQ(src.name(), "fake");
  EXPECT_EQ(src.source_date(), date);
}

}  // namespace
}  // namespace sk::ingest
