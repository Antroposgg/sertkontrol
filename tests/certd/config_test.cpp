#include "config.hpp"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace sk::certd {
namespace {

EnvLookup env_of(std::map<std::string, std::string, std::less<>> vars) {
  return [vars = std::move(vars)](std::string_view name) -> std::optional<std::string> {
    if (const auto it = vars.find(name); it != vars.end()) {
      return it->second;
    }
    return std::nullopt;
  };
}

TEST(Config, Defaults) {
  const auto cfg = load_config(env_of({}));
  ASSERT_TRUE(cfg);
  EXPECT_EQ(cfg.value().port, 8080);
  EXPECT_EQ(cfg.value().threads, 0U);
  EXPECT_EQ(cfg.value().db_connections, 4U);
  EXPECT_EQ(cfg.value().pg_conninfo, "host='postgres' port=5432 dbname='sertkontrol' user='sertkontrol'");
  EXPECT_EQ(cfg.value().web_root, "/srv/app");
  EXPECT_EQ(cfg.value().snapshot_dir, "/data/snapshots");
}

TEST(Config, Overrides) {
  const auto cfg = load_config(env_of({{"CERTD_PORT", "9000"},
                                       {"CERTD_THREADS", "2"},
                                       {"CERTD_DB_CONNECTIONS", "8"},
                                       {"POSTGRES_HOST", "db"},
                                       {"POSTGRES_PORT", "6543"},
                                       {"POSTGRES_PASSWORD", "p'w\\d"},
                                       {"WEB_ROOT", "/tmp/web"},
                                       {"SNAPSHOT_DIR", "/tmp/snap"}}));
  ASSERT_TRUE(cfg);
  EXPECT_EQ(cfg.value().port, 9000);
  EXPECT_EQ(cfg.value().threads, 2U);
  EXPECT_EQ(cfg.value().db_connections, 8U);
  EXPECT_EQ(cfg.value().pg_conninfo,
            R"(host='db' port=6543 dbname='sertkontrol' user='sertkontrol' password='p\'w\\d')");
  EXPECT_EQ(cfg.value().web_root, "/tmp/web");
  EXPECT_EQ(cfg.value().snapshot_dir, "/tmp/snap");
}

TEST(Config, EmptyValuesMeanDefault) {
  const auto cfg =
      load_config(env_of({{"CERTD_PORT", ""}, {"POSTGRES_HOST", ""}, {"POSTGRES_PASSWORD", ""}}));
  ASSERT_TRUE(cfg);
  EXPECT_EQ(cfg.value().port, 8080);
  EXPECT_EQ(cfg.value().pg_conninfo.find("password"), std::string::npos);
}

class ConfigInvalid : public ::testing::TestWithParam<std::pair<const char*, const char*>> {};

TEST_P(ConfigInvalid, Rejected) {
  const auto [name, value] = GetParam();
  const auto cfg = load_config(env_of({{name, value}}));
  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error().code, ErrorCode::kInvalidArgument);
  EXPECT_NE(cfg.error().detail.find(name), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(All, ConfigInvalid,
                         ::testing::Values(std::pair{"CERTD_PORT", "0"}, std::pair{"CERTD_PORT", "65536"},
                                           std::pair{"CERTD_PORT", "80x"}, std::pair{"CERTD_PORT", "-1"},
                                           std::pair{"CERTD_THREADS", "1000"},
                                           std::pair{"POSTGRES_PORT", "abc"},
                                           std::pair{"CERTD_DB_CONNECTIONS", "0"}));

TEST(Config, MaxSettings) {
  const auto off = load_config(env_of({}));
  ASSERT_TRUE(off);
  EXPECT_FALSE(off.value().bot_enabled());
  EXPECT_EQ(off.value().max_api_base_url, "https://platform-api2.max.ru");
  EXPECT_EQ(off.value().recog_threads, 2U);
  EXPECT_EQ(off.value().recog_queue, 8U);

  const auto on = load_config(env_of({{"MAX_BOT_TOKEN", "t"},
                                      {"MAX_WEBHOOK_SECRET", "Secret_123-x"},
                                      {"MAX_BOT_USERNAME", "sertkontrol_bot"},
                                      {"CERTD_RECOG_THREADS", "4"}}));
  ASSERT_TRUE(on) << on.error().detail;
  EXPECT_TRUE(on.value().bot_enabled());
  EXPECT_EQ(on.value().max_bot_username, "sertkontrol_bot");
  EXPECT_EQ(on.value().recog_threads, 4U);

  EXPECT_FALSE(load_config(env_of({{"MAX_BOT_TOKEN", "t"}})));         // без секрета
  EXPECT_FALSE(load_config(env_of({{"MAX_WEBHOOK_SECRET", "abc"}})));  // короче 5
  EXPECT_FALSE(load_config(env_of({{"MAX_WEBHOOK_SECRET", "bad secret!"}})));
  EXPECT_FALSE(load_config(env_of({{"CERTD_RECOG_QUEUE", "0"}})));
  EXPECT_FALSE(load_config(env_of({{"CERTD_DEV_USER_ID", "x"}})));
}

// ADR-0013: dev-пользователь работает только без токена бота.
TEST(Config, DevUserOnlyWithoutBotToken) {
  const auto dev = load_config(env_of({{"CERTD_DEV_USER_ID", "1000001"}}));
  ASSERT_TRUE(dev);
  EXPECT_EQ(dev.value().dev_user_id, 1000001);
  EXPECT_FALSE(dev.value().dev_user_id_ignored);

  const auto prod = load_config(
      env_of({{"CERTD_DEV_USER_ID", "1000001"}, {"MAX_BOT_TOKEN", "t"}, {"MAX_WEBHOOK_SECRET", "secret-1"}}));
  ASSERT_TRUE(prod);
  EXPECT_FALSE(prod.value().dev_user_id.has_value());
  EXPECT_TRUE(prod.value().dev_user_id_ignored);
}

TEST(Config, QuoteEscapes) {
  EXPECT_EQ(conninfo_quote(""), "''");
  EXPECT_EQ(conninfo_quote("a b"), "'a b'");
  EXPECT_EQ(conninfo_quote(R"(a'b\c)"), R"('a\'b\\c')");
}

TEST(Config, ProcessEnv) {
  EXPECT_EQ(process_env("SK_SURELY_UNDEFINED_VARIABLE_42"), std::nullopt);
  EXPECT_TRUE(process_env("PATH").has_value());
}

}  // namespace
}  // namespace sk::certd
