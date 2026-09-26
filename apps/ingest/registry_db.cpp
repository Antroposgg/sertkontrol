#include "registry_db.hpp"

#include <vector>

#include <libpq-fe.h>

namespace sk::ingest {

namespace {

std::string quote(std::string_view value) {
  std::string out{"'"};
  for (const char c : value) {
    if (c == '\\' || c == '\'') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

std::string get_or(const EnvLookup& env, std::string_view name, std::string_view fallback) {
  auto v = env(name);
  return v && !v->empty() ? std::move(*v) : std::string{fallback};
}

std::string iso(Date d) {
  const std::chrono::year_month_day ymd{d};
  const auto y = static_cast<int>(ymd.year());
  const auto m = static_cast<unsigned>(ymd.month());
  const auto dd = static_cast<unsigned>(ymd.day());
  std::string out = std::to_string(y) + "-";
  out += (m < 10 ? "0" : "") + std::to_string(m) + "-";
  out += (dd < 10 ? "0" : "") + std::to_string(dd);
  return out;
}

}  // namespace

std::string pg_conninfo(const EnvLookup& env) {
  // Та же схема, что в apps/certd/config.cpp: ingest и certd настраиваются одними переменными.
  std::string info = "host=" + quote(get_or(env, "POSTGRES_HOST", "postgres")) +
                     " port=" + quote(get_or(env, "POSTGRES_PORT", "5432")) +
                     " dbname=" + quote(get_or(env, "POSTGRES_DB", "sertkontrol")) +
                     " user=" + quote(get_or(env, "POSTGRES_USER", "sertkontrol"));
  if (const auto pw = env("POSTGRES_PASSWORD"); pw && !pw->empty()) {
    info += " password=" + quote(*pw);
  }
  return info;
}

struct RegistryDb::Conn {
  explicit Conn(PGconn* c) : pg(c) {}
  Conn(const Conn&) = delete;
  Conn& operator=(const Conn&) = delete;
  Conn(Conn&&) = delete;
  Conn& operator=(Conn&&) = delete;
  ~Conn() { PQfinish(pg); }
  PGconn* pg;
};

Result<RegistryDb> RegistryDb::connect(const std::string& conninfo) {
  auto conn = std::make_shared<Conn>(PQconnectdb(conninfo.c_str()));
  if (PQstatus(conn->pg) != CONNECTION_OK) {
    return Error{ErrorCode::kInternal, std::string{"PostgreSQL: "} + PQerrorMessage(conn->pg)};
  }
  return RegistryDb{std::move(conn)};
}

Result<Ok> RegistryDb::exec(const char* sql, const std::vector<std::string>& params) {
  std::vector<const char*> values;
  values.reserve(params.size());
  for (const auto& p : params) {
    values.push_back(p.c_str());
  }
  PGresult* res = PQexecParams(conn_->pg, sql, static_cast<int>(values.size()), nullptr, values.data(),
                               nullptr, nullptr, 0);
  const bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
  const std::string error = ok ? std::string{} : std::string{PQresultErrorMessage(res)};
  PQclear(res);
  if (!ok) {
    return Error{ErrorCode::kInternal, "PostgreSQL: " + error};
  }
  return Ok{};
}

Result<Ok> RegistryDb::mark_building(std::uint64_t version, std::string_view source, Date source_date,
                                     const std::string& file_path, bool is_demo) {
  return exec(
      "INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo) "
      "VALUES ($1, $2, $3, $4, 'building', $5) "
      "ON CONFLICT (version) DO UPDATE SET source = EXCLUDED.source, source_date = EXCLUDED.source_date, "
      "file_path = EXCLUDED.file_path, status = 'building', is_demo = EXCLUDED.is_demo, "
      "record_count = 0, stats = '{}'::jsonb",
      {std::to_string(version), std::string{source}, iso(source_date), file_path,
       is_demo ? "true" : "false"});
}

Result<Ok> RegistryDb::mark_ready(std::uint64_t version, const BuildResult& build) {
  const auto stats = "{\"records\":" + std::to_string(build.stats.records) +
                     ",\"duplicates\":" + std::to_string(build.stats.duplicates) +
                     ",\"rejected\":" + std::to_string(build.rejected) +
                     ",\"bytes\":" + std::to_string(build.stats.bytes) + "}";
  return exec(
      "UPDATE snapshot_version SET status = 'ready', record_count = $2, stats = $3::jsonb WHERE version = $1",
      {std::to_string(version), std::to_string(build.stats.records), stats});
}

Result<Ok> RegistryDb::mark_failed(std::uint64_t version, const std::string& reason) {
  return exec(
      "UPDATE snapshot_version SET status = 'failed', stats = jsonb_build_object('error', $2::text) "
      "WHERE version = $1",
      {std::to_string(version), reason});
}

}  // namespace sk::ingest
