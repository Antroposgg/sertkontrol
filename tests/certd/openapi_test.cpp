/// Лёгкий контрактный тест C7: пути и методы openapi.yaml совпадают с маршрутами certd.
/// Полные контрактные тесты по схемам (schemathesis) и линтер — этап 3.
#include <drogon/drogon.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include <yaml-cpp/yaml.h>

#include "fake_domain.hpp"
#include "rest_api.hpp"
#include "sertkontrol/fakes.hpp"

namespace sk::certd {
namespace {

std::string method_name(drogon::HttpMethod m) {
  switch (m) {
    case drogon::Get:
      return "get";
    case drogon::Post:
      return "post";
    case drogon::Delete:
      return "delete";
    default:
      return "other";
  }
}

TEST(OpenApi, PathsMatchRegisteredRoutes) {
  const auto doc = YAML::LoadFile(SK_OPENAPI_YAML);
  ASSERT_EQ(doc["openapi"].as<std::string>(), "3.1.0");
  std::set<std::string> documented;
  for (const auto& path : doc["paths"]) {
    auto p = path.first.as<std::string>();
    // OpenAPI {id} ↔ Drogon {1}
    if (const auto b = p.find('{'); b != std::string::npos) {
      p = p.substr(0, b) + "{1}";
    }
    for (const auto& op : path.second) {
      documented.insert(op.first.as<std::string>() + " " + p);
      EXPECT_TRUE(op.second["responses"].IsMap()) << p;
      EXPECT_TRUE(op.second["operationId"].IsScalar()) << p;
    }
  }

  using std::chrono::day;
  using std::chrono::month;
  using std::chrono::year;
  FakeDomainService domain{fake::FakeSnapshot::three_records(), fake::FakeSnapshot::three_records(),
                           year{2026} / month{9} / day{26}};
  RestApi::register_routes(drogon::app(), std::make_shared<RestApi>(domain, AuthConfig{}));
  std::set<std::string> registered{"get /healthz", "post /max/webhook"};  // регистрируются в main.cpp
  for (const auto& [path, method, desc] : drogon::app().getHandlersInfo()) {
    // Обработчики-«405» — не операции API, а замена ответа Drogon на чужой метод.
    if (path.starts_with("/api/") && !is_explicit_405(path, method)) {
      registered.insert(method_name(method) + " " + path);
    }
  }
  EXPECT_EQ(documented, registered);
}

TEST(OpenApi, ProblemCodesMatchErrorCode) {
  const auto doc = YAML::LoadFile(SK_OPENAPI_YAML);
  std::set<std::string> codes;
  for (const auto& c : doc["components"]["schemas"]["Problem"]["properties"]["code"]["enum"]) {
    codes.insert(c.as<std::string>());
  }
  std::set<std::string> expected;
  for (int i = 0; i <= static_cast<int>(ErrorCode::kInternal); ++i) {
    expected.insert(std::string{to_string(static_cast<ErrorCode>(i))});
  }
  EXPECT_EQ(codes, expected);
}

}  // namespace
}  // namespace sk::certd
