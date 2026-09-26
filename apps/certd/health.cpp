#include "health.hpp"

#include <string>

namespace sk::certd {

HealthReport evaluate_health(const HealthInputs& in) {
  const bool snapshot_ok = in.snapshot_version.has_value() || !in.snapshot_required;
  const bool healthy = in.db_ok && snapshot_ok;
  const std::string snapshot =
      in.snapshot_version ? std::to_string(*in.snapshot_version) : std::string{"null"};
  HealthReport report;
  report.http_status = healthy ? 200 : 503;
  report.body = std::string{R"({"status":")"} + (healthy ? "ok" : "unavailable") + R"(","db":")" +
                (in.db_ok ? "ok" : "down") + R"(","snapshot_version":)" + snapshot + "}";
  return report;
}

}  // namespace sk::certd
