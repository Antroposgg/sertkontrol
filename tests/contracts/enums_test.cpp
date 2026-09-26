#include <gtest/gtest.h>

#include <set>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk {
namespace {

TEST(ErrorCode, NamesAreUniqueSnakeCase) {
  std::set<std::string_view> names;
  for (int i = 0; i <= static_cast<int>(ErrorCode::kInternal); ++i) {
    const auto name = to_string(static_cast<ErrorCode>(i));
    EXPECT_TRUE(names.insert(name).second) << name;
    EXPECT_EQ(name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_"), std::string_view::npos) << name;
  }
  // Коды, которые АРХ §8 называет явно.
  EXPECT_EQ(to_string(ErrorCode::kInitDataExpired), "init_data_expired");
  EXPECT_EQ(to_string(ErrorCode::kFileTooLarge), "file_too_large");
  EXPECT_EQ(to_string(ErrorCode::kNumberNotRecognized), "number_not_recognized");
  EXPECT_EQ(to_string(ErrorCode::kNotFoundInSnapshot), "not_found_in_snapshot");
}

TEST(Status, RoundTrip) {
  for (int i = 0; i <= static_cast<int>(snapshot::Status::kArchived); ++i) {
    const auto s = static_cast<snapshot::Status>(i);
    EXPECT_EQ(snapshot::status_from_string(snapshot::to_string(s)), s);
  }
  EXPECT_EQ(snapshot::status_from_string("действует"), std::nullopt);
}

TEST(Canon, VersionIsPositive) {
  static_assert(canon::kVersion >= 1);
}

}  // namespace
}  // namespace sk
