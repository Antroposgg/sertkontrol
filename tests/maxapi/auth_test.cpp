#include "sertkontrol/maxapi/auth.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "support/init_data.hpp"

namespace sk::maxapi {
namespace {

// Вектор посчитан независимой реализацией официального алгоритма (Python hmac/hashlib, docs/plan.md §5.1).
constexpr std::string_view kToken = "test-bot-token-123:ABC";
constexpr std::string_view kInitData =
    "auth_date=1790000000&chat=%7B%22id%22%3A12345%2C%22type%22%3A%22DIALOG%22%7D&ip=192.168.0.1"
    "&query_id=4c0ab423-342b-4e45-aea4-2747dbc500cd&start_param=doc-RUD-CR"
    "&user=%7B%22id%22%3A67890%2C%22first_name%22%3A%22%D0%9C%D0%B0%D0%BA%D1%81%22%2C%22last_name%22%3A%22%"
    "D0%A2%D0%"
    "B5%D1%81%D1%82%D0%BE%D0%B2%22%2C%22username%22%3Anull%2C%22language_code%22%3A%22ru%22%2C%22photo_url%"
    "22%"
    "3Anull%7D&hash=03ba8861d29ec735fdeaff0f55996da90819b2c22d40f61c9e4c96b254d71b96";

std::chrono::system_clock::time_point issued() {
  return std::chrono::system_clock::time_point{std::chrono::seconds{1790000000}};
}
std::chrono::system_clock::time_point now() {
  return issued() + std::chrono::hours{1};
}

TEST(InitData, ValidVector) {
  const auto r = validate_init_data(kInitData, kToken, now());
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  EXPECT_EQ(r.value().user_id, 67890);
  EXPECT_EQ(r.value().first_name, "Макс");
  EXPECT_EQ(r.value().auth_date, 1790000000);
  EXPECT_EQ(r.value().start_param, "doc-RUD-CR");
}

TEST(InitData, WholeStringEncodedOnceMore) {
  EXPECT_TRUE(validate_init_data(percent_encode(kInitData), kToken, now()).has_value());
}

TEST(InitData, Expired) {
  const auto r = validate_init_data(kInitData, kToken, issued() + std::chrono::hours{25});
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::kInitDataExpired);
}

TEST(InitData, Forgeries) {
  const std::string data{kInitData};
  const auto tampered_user = [&] {
    auto s = data;
    s.replace(s.find("67890"), 5, "11111");
    return s;
  }();
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"wrong token", data},
      {"tampered user", tampered_user},
      {"no hash", data.substr(0, data.find("&hash="))},
      {"double hash", data + "&hash=00"},
      {"repeated key", data + "&ip=1.1.1.1"},
      {"bad pair", "noequals&" + data},
      {"bad percent", "user=%ZZ&" + data},
      {"empty", ""},
  };
  for (const auto& [name, raw] : cases) {
    SCOPED_TRACE(name);
    const auto token = name == "wrong token" ? std::string_view{"other"} : kToken;
    const auto r = validate_init_data(raw, token, now());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::kUnauthorized);
  }
  EXPECT_EQ(validate_init_data(kInitData, "", now()).error().code, ErrorCode::kUnauthorized);
}

std::string sign(const std::vector<std::pair<std::string, std::string>>& p) {
  return test::sign_init_data(kToken, p);
}

TEST(InitData, SignedButIncomplete) {
  const std::string user = R"({"id":1})";
  const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> cases = {
      {"no auth_date", {{"user", user}}},
      {"bad auth_date", {{"auth_date", "вчера"}, {"user", user}}},
      {"no user", {{"auth_date", "1790000000"}}},
      {"bad user", {{"auth_date", "1790000000"}, {"user", "{"}}},
      {"user without id", {{"auth_date", "1790000000"}, {"user", R"({"first_name":"x"})"}}},
  };
  for (const auto& [name, params] : cases) {
    SCOPED_TRACE(name);
    const auto r = validate_init_data(sign(params), kToken, now());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::kUnauthorized);
  }
  const auto ok = validate_init_data(sign({{"auth_date", "1790000000"}, {"user", user}}), kToken, now());
  ASSERT_TRUE(ok.has_value()) << ok.error().detail;
  EXPECT_EQ(ok.value().user_id, 1);
  EXPECT_FALSE(ok.value().start_param.has_value());
}

TEST(Secret, ConstantTimeCompare) {
  EXPECT_TRUE(secret_matches("s3cret-Value_1", "s3cret-Value_1"));
  EXPECT_FALSE(secret_matches("s3cret-Value_2", "s3cret-Value_1"));
  EXPECT_FALSE(secret_matches("short", "s3cret-Value_1"));
  EXPECT_FALSE(secret_matches("", ""));
}

TEST(Percent, RoundTrip) {
  EXPECT_EQ(percent_encode("a b/ю~"), "a%20b%2F%D1%8E~");
  EXPECT_EQ(percent_decode("a%20b%2F%D1%8E~"), "a b/ю~");
  EXPECT_EQ(percent_decode("a+b"), "a+b");
  EXPECT_FALSE(percent_decode("%4").has_value());
  EXPECT_FALSE(percent_decode("%G0").has_value());
}

}  // namespace
}  // namespace sk::maxapi
