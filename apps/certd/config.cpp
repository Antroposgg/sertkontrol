#include "config.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>

namespace sk::certd {

namespace {

std::string get_or(const EnvLookup& env, std::string_view name, std::string_view fallback) {
  auto value = env(name);
  return (value && !value->empty()) ? std::move(*value) : std::string{fallback};
}

/// Целое из окружения в диапазоне [lo, hi]; пустое/отсутствующее → fallback.
Result<std::uint64_t> get_uint(const EnvLookup& env, std::string_view name, std::uint64_t fallback,
                               std::uint64_t lo, std::uint64_t hi) {
  const auto value = env(name);
  if (!value || value->empty()) {
    return fallback;
  }
  std::uint64_t parsed = 0;
  const auto* end = value->data() + value->size();
  const auto [ptr, ec] = std::from_chars(value->data(), end, parsed);
  if (ec != std::errc{} || ptr != end || parsed < lo || parsed > hi) {
    return Error{ErrorCode::kInvalidArgument, std::string{name} + ": ожидается целое в диапазоне [" +
                                                  std::to_string(lo) + ", " + std::to_string(hi) +
                                                  "], получено «" + *value + "»"};
  }
  return parsed;
}

bool secret_format_ok(std::string_view s) {
  return s.size() >= 5 && s.size() <= 256 && std::ranges::all_of(s, [](char c) {
           return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '-';
         });
}

}  // namespace

std::optional<std::string> process_env(std::string_view name) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): читаем окружение один раз при старте, до запуска потоков.
  const char* value = std::getenv(std::string{name}.c_str());
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string{value};
}

std::string conninfo_quote(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('\'');
  for (const char c : value) {
    if (c == '\\' || c == '\'') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

Result<Config> load_config(const EnvLookup& env) {
  Config cfg;
  const auto port = get_uint(env, "CERTD_PORT", 8080, 1, std::numeric_limits<std::uint16_t>::max());
  if (!port) {
    return port.error();
  }
  const auto threads = get_uint(env, "CERTD_THREADS", 0, 0, 256);
  if (!threads) {
    return threads.error();
  }
  const auto pg_port = get_uint(env, "POSTGRES_PORT", 5432, 1, std::numeric_limits<std::uint16_t>::max());
  if (!pg_port) {
    return pg_port.error();
  }
  const auto db_connections = get_uint(env, "CERTD_DB_CONNECTIONS", 4, 1, 64);
  if (!db_connections) {
    return db_connections.error();
  }
  cfg.port = static_cast<std::uint16_t>(port.value());
  cfg.threads = static_cast<std::size_t>(threads.value());
  cfg.db_connections = static_cast<std::size_t>(db_connections.value());

  cfg.pg_conninfo = "host=" + conninfo_quote(get_or(env, "POSTGRES_HOST", "postgres")) +
                    " port=" + std::to_string(pg_port.value()) +
                    " dbname=" + conninfo_quote(get_or(env, "POSTGRES_DB", "sertkontrol")) +
                    " user=" + conninfo_quote(get_or(env, "POSTGRES_USER", "sertkontrol"));
  if (const auto password = env("POSTGRES_PASSWORD"); password && !password->empty()) {
    cfg.pg_conninfo += " password=" + conninfo_quote(*password);
  }
  cfg.snapshot_dir = get_or(env, "SNAPSHOT_DIR", cfg.snapshot_dir.string());
  cfg.web_root = get_or(env, "WEB_ROOT", cfg.web_root.string());

  cfg.max_bot_token = get_or(env, "MAX_BOT_TOKEN", "");
  cfg.max_webhook_secret = get_or(env, "MAX_WEBHOOK_SECRET", "");
  cfg.max_bot_username = get_or(env, "MAX_BOT_USERNAME", "");
  cfg.max_api_base_url = get_or(env, "MAX_API_BASE_URL", cfg.max_api_base_url);
  if (!cfg.max_webhook_secret.empty() && !secret_format_ok(cfg.max_webhook_secret)) {
    return Error{ErrorCode::kInvalidArgument, "MAX_WEBHOOK_SECRET: 5–256 символов A-Z, a-z, 0-9, _ и -"};
  }
  if (cfg.bot_enabled() && cfg.max_webhook_secret.empty()) {
    return Error{ErrorCode::kInvalidArgument, "MAX_WEBHOOK_SECRET обязателен, если задан MAX_BOT_TOKEN"};
  }
  const auto dev = get_uint(env, "CERTD_DEV_USER_ID", 0, 0, std::numeric_limits<std::int64_t>::max());
  if (!dev) {
    return dev.error();
  }
  // ADR-0013: вход без initData возможен только там, где подпись проверить нечем. С токеном бота
  // dev-пользователь игнорируется (main.cpp предупреждает в логе), чтобы один compose.yaml годился и
  // локально, и на VPS.
  cfg.dev_user_id_ignored = dev.value() != 0 && cfg.bot_enabled();
  if (dev.value() != 0 && !cfg.bot_enabled()) {
    cfg.dev_user_id = static_cast<std::int64_t>(dev.value());
  }
  const auto recog_threads = get_uint(env, "CERTD_RECOG_THREADS", 2, 1, 32);
  if (!recog_threads) {
    return recog_threads.error();
  }
  const auto queue = get_uint(env, "CERTD_RECOG_QUEUE", 8, 1, 256);
  if (!queue) {
    return queue.error();
  }
  cfg.recog_threads = static_cast<std::size_t>(recog_threads.value());
  cfg.recog_queue = static_cast<std::size_t>(queue.value());
  return cfg;
}

}  // namespace sk::certd
