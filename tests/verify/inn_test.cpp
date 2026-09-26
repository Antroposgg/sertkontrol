#include "sertkontrol/verify/inn.hpp"

#include <gtest/gtest.h>

namespace sk::verify {
namespace {

TEST(Inn, TenDigits) {
  EXPECT_TRUE(inn_valid("5047270690"));  // пример из АРХ §7.7: сумма 252, 252 mod 11 = 10 → 0
  EXPECT_TRUE(inn_valid("7700000016"));  // вымышленный ИНН демо-данных
  EXPECT_FALSE(inn_valid("5047270691"));
}

TEST(Inn, TwelveDigits) {
  EXPECT_TRUE(inn_valid("500123456750"));
  EXPECT_FALSE(inn_valid("500123456751"));
  EXPECT_FALSE(inn_valid("500123456740"));
}

TEST(Inn, Format) {
  for (const char* bad : {"", "123", "50472706901", "504727069a", "５０４７２７０６９０"}) {
    EXPECT_FALSE(inn_valid(bad)) << bad;
  }
}

}  // namespace
}  // namespace sk::verify
