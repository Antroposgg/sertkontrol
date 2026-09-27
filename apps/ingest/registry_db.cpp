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

/// JSON-строка с экранированием по RFC 8259 §7 (кавычка, обратная черта, управляющие символы).
std::string json_string(std::string_view s) {
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string out{"\""};
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (u < 0x20) {
      out += "\\u00";
      out.push_back(kHex[u >> 4U]);
      out.push_back(kHex[u & 0xFU]);
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

std::string json_date(const std::optional<Date>& d) {
  return d ? json_string(iso(*d)) : std::string{"null"};
}

/// Строки `registry_change` одной версии как JSON-массив для `jsonb_to_recordset`.
std::string changes_json(const std::vector<snapshot::DocChange>& changes) {
  std::string out{"["};
  for (const auto& c : changes) {
    if (out.size() > 1) {
      out.push_back(',');
    }
    const auto& date = c.after ? c.after->status_date : std::optional<Date>{};
    out += "{\"doc_key\":" + json_string(c.doc_key) +
           ",\"before\":" + (c.before ? doc_state_json(*c.before) : std::string{"null"}) +
           ",\"after\":" + (c.after ? doc_state_json(*c.after) : std::string{"null"}) +
           ",\"status_date\":" + json_date(date) + "}";
  }
  out.push_back(']');
  return out;
}

}  // namespace

std::string doc_state_json(const snapshot::DocState& state) {
  return "{\"status\":" + json_string(snapshot::to_string(state.status)) +
         ",\"expiry_date\":" + json_date(state.expiry_date) +
         ",\"status_date\":" + json_date(state.status_date) + "}";
}

std::string stats_json(const BuildResult& build, const snapshot::DiffStats* diff,
                       std::size_t written_changes) {
  std::string out = "{\"records\":" + std::to_string(build.stats.records) +
                    ",\"duplicates\":" + std::to_string(build.stats.duplicates) +
                    ",\"rejected\":" + std::to_string(build.rejected) +
                    ",\"bytes\":" + std::to_string(build.stats.bytes) +
                    ",\"changes_written\":" + std::to_string(written_changes);
  if (diff != nullptr) {
    out += ",\"diff\":{\"added\":" + std::to_string(diff->added) +
           ",\"removed\":" + std::to_string(diff->removed) + ",\"changed\":" + std::to_string(diff->changed) +
           ",\"unchanged\":" + std::to_string(diff->unchanged) + ",\"transitions\":{";
    bool first = true;
    for (const auto& [from_to, n] : diff->transitions) {
      if (!first) {
        out.push_back(',');
      }
      first = false;
      out += json_string(std::string{snapshot::to_string(from_to.first)} + "->" +
                         std::string{snapshot::to_string(from_to.second)}) +
             ":" + std::to_string(n);
    }
    out += "}}";
  }
  out.push_back('}');
  return out;
}

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

Result<Ok> RegistryDb::exec(const char* sql, const std::vector<std::optional<std::string>>& params) {
  std::vector<const char*> values;
  values.reserve(params.size());
  for (const auto& p : params) {
    values.push_back(p ? p->c_str() : nullptr);  // nullptr — SQL NULL
  }
  PGresult* res = PQexecParams(conn_->pg, sql, static_cast<int>(values.size()), nullptr, values.data(),
                               nullptr, nullptr, 0);
  const auto status = PQresultStatus(res);
  const bool ok = status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
  const std::string error = ok ? std::string{} : std::string{PQresultErrorMessage(res)};
  PQclear(res);
  if (!ok) {
    return Error{ErrorCode::kInternal, "PostgreSQL: " + error};
  }
  return Ok{};
}

Result<Ok> RegistryDb::mark_building(const VersionInfo& info) {
  std::optional<std::string> stage;
  if (info.demo_stage) {
    stage = std::to_string(*info.demo_stage);
  }
  return exec(
      "INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo, demo_stage) "
      "VALUES ($1, $2, $3, $4, 'building', $5, $6::smallint) "
      "ON CONFLICT (version) DO UPDATE SET source = EXCLUDED.source, source_date = EXCLUDED.source_date, "
      "file_path = EXCLUDED.file_path, status = 'building', is_demo = EXCLUDED.is_demo, "
      "demo_stage = EXCLUDED.demo_stage, record_count = 0, stats = '{}'::jsonb",
      {std::to_string(info.version), info.source, iso(info.source_date), info.file_path,
       std::string{info.is_demo ? "true" : "false"}, stage});
}

Result<Ok> RegistryDb::publish(std::uint64_t version, const BuildResult& build,
                               const std::vector<snapshot::DocChange>& changes,
                               const snapshot::DiffStats* diff) {
  const auto v = std::to_string(version);
  if (auto r = exec("BEGIN", {}); !r) {
    return r;
  }
  auto step = [&]() -> Result<Ok> {
    // Повторная сборка той же версии заменяет её изменения целиком (демо собирается при каждом старте).
    if (auto r = exec("DELETE FROM registry_change WHERE version = $1", {v}); !r) {
      return r;
    }
    if (!changes.empty()) {
      if (auto r = exec("INSERT INTO registry_change (version, doc_key, before, after, status_date) "
                        "SELECT $1::bigint, x.doc_key, x.before, x.after, x.status_date "
                        "FROM jsonb_to_recordset($2::jsonb) AS x(doc_key text, before jsonb, after jsonb, "
                        "status_date date)",
                        {v, changes_json(changes)});
          !r) {
        return r;
      }
    }
    if (auto r = exec("UPDATE snapshot_version SET status = 'ready', record_count = $2, stats = $3::jsonb "
                      "WHERE version = $1",
                      {v, std::to_string(build.stats.records), stats_json(build, diff, changes.size())});
        !r) {
      return r;
    }
    return exec("SELECT pg_notify($1, $2)", {std::string{kSnapshotReadyChannel}, v});
  };
  if (auto r = step(); !r) {
    (void)exec("ROLLBACK", {});
    return r;
  }
  return exec("COMMIT", {});
}

Result<Ok> RegistryDb::mark_failed(std::uint64_t version, const std::string& reason) {
  return exec(
      "UPDATE snapshot_version SET status = 'failed', stats = jsonb_build_object('error', $2::text) "
      "WHERE version = $1",
      {std::to_string(version), reason});
}

}  // namespace sk::ingest
