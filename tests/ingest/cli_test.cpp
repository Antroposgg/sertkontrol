#include "cli.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

namespace sk::ingest {
namespace {

Result<Options> parse(std::vector<std::string_view> args) {
  return parse_args(args);
}

TEST(IngestCli, Modes) {
  EXPECT_EQ(parse({"--once"}).value().mode, Mode::kOnce);
  EXPECT_EQ(parse({"--daemon"}).value().mode, Mode::kDaemon);
  EXPECT_EQ(parse({"--demo"}).value().mode, Mode::kDemo);
  EXPECT_EQ(parse({"--help"}).value().mode, Mode::kHelp);
  EXPECT_EQ(parse({"-h"}).value().mode, Mode::kHelp);
  EXPECT_EQ(parse({"--demo", "--demo"}).value().mode, Mode::kDemo);
}

TEST(IngestCli, Paths) {
  const auto o = parse({"--snapshot-dir", "/s", "--demo", "--demo-dir", "/d"});
  ASSERT_TRUE(o);
  EXPECT_EQ(o.value().snapshot_dir, "/s");
  EXPECT_EQ(o.value().demo_dir, "/d");
}

TEST(IngestCli, Errors) {
  EXPECT_FALSE(parse({}));
  EXPECT_FALSE(parse({"--once", "--demo"}));
  EXPECT_FALSE(parse({"--demo", "--snapshot-dir"}));
  EXPECT_FALSE(parse({"--frobnicate"}));
  EXPECT_EQ(parse({"--x"}).error().code, ErrorCode::kInvalidArgument);
}

TEST(IngestCli, UsageMentionsAllModes) {
  for (const std::string_view m : {"--once", "--daemon", "--demo"}) {
    EXPECT_NE(usage().find(m), std::string_view::npos);
  }
}

}  // namespace
}  // namespace sk::ingest
