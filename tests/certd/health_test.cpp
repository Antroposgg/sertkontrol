#include "health.hpp"

#include <gtest/gtest.h>

namespace sk::certd {
namespace {

TEST(Health, OkWhenDbUpAndSnapshotNotRequired) {
  const auto r = evaluate_health({.db_ok = true});
  EXPECT_EQ(r.http_status, 200);
  EXPECT_EQ(r.body, R"({"status":"ok","db":"ok","snapshot_version":null})");
}

TEST(Health, UnavailableWhenDbDown) {
  const auto r = evaluate_health({.db_ok = false});
  EXPECT_EQ(r.http_status, 503);
  EXPECT_EQ(r.body, R"({"status":"unavailable","db":"down","snapshot_version":null})");
}

TEST(Health, RequiredSnapshotMissing) {
  const auto r = evaluate_health({.db_ok = true, .snapshot_required = true});
  EXPECT_EQ(r.http_status, 503);
}

TEST(Health, RequiredSnapshotLoaded) {
  const auto r = evaluate_health({.db_ok = true, .snapshot_version = 7, .snapshot_required = true});
  EXPECT_EQ(r.http_status, 200);
  EXPECT_EQ(r.body, R"({"status":"ok","db":"ok","snapshot_version":7})");
}

}  // namespace
}  // namespace sk::certd
