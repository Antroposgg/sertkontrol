#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>

#include "sertkontrol_contracts.hpp"

namespace sk {
namespace {

TEST(Result, HoldsValue) {
  Result<int> r{42};
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(static_cast<bool>(r));
  EXPECT_EQ(r.value(), 42);
  EXPECT_THROW((void)r.error(), std::logic_error);
}

TEST(Result, HoldsError) {
  const Result<int> r{Error{ErrorCode::kNotFound, "нет"}};
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
  EXPECT_EQ(r.error().detail, "нет");
  EXPECT_THROW((void)r.value(), std::bad_optional_access);
}

TEST(Result, MovesValueOut) {
  Result<std::string> r{std::string(64, 'x')};
  const std::string s = std::move(r).value();
  EXPECT_EQ(s.size(), 64U);
}

TEST(Result, RvalueValueOfErrorThrows) {
  EXPECT_THROW((void)Result<int>{Error{}}.value(), std::bad_optional_access);
}

TEST(Result, MutableValue) {
  Result<int> r{1};
  r.value() = 2;
  EXPECT_EQ(r.value(), 2);
}

}  // namespace
}  // namespace sk
