#include "rest_api.hpp"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include <json/json.h>

#include "fake_domain.hpp"
#include "sertkontrol/fakes.hpp"
#include "support/files.hpp"
#include "support/init_data.hpp"

namespace sk::certd {
namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr std::string_view kToken = "rest-test-token";
constexpr long long kAuthDate = 1790000000;
constexpr long long kAlice = 101;
constexpr long long kBob = 202;

std::chrono::system_clock::time_point test_now() {
  return std::chrono::system_clock::time_point{std::chrono::seconds{kAuthDate + 60}};
}

class RestApiTest : public ::testing::Test {
 public:
  FakeDomainService domain{fake::FakeSnapshot::three_records(), fake::FakeSnapshot::three_records(),
                           year{2026} / month{9} / day{26}};
  RestApi api{domain, AuthConfig{.bot_token = std::string{kToken}, .now = test_now}};

  static drogon::HttpRequestPtr as(long long user, drogon::HttpMethod method = drogon::Get) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(method);
    req->addHeader("X-Max-Init-Data", test::init_data_for(kToken, user, kAuthDate));
    return req;
  }

  static drogon::HttpRequestPtr json_as(long long user, const Json::Value& body) {
    auto req = drogon::HttpRequest::newHttpJsonRequest(body);
    req->setMethod(drogon::Post);
    req->addHeader("X-Max-Init-Data", test::init_data_for(kToken, user, kAuthDate));
    return req;
  }

  static Json::Value body(const drogon::HttpResponsePtr& resp) {
    Json::Value v;
    const Json::CharReaderBuilder b;
    const std::unique_ptr<Json::CharReader> r{b.newCharReader()};
    std::string errs;
    const auto s = std::string{resp->body()};
    r->parse(s.data(), s.data() + s.size(), &v, &errs);
    return v;
  }

  static void expect_problem(const drogon::HttpResponsePtr& resp, int status, const std::string& code) {
    EXPECT_EQ(static_cast<int>(resp->getStatusCode()), status);
    EXPECT_NE(resp->contentTypeString().find("application/problem+json"), std::string::npos);
    const auto v = body(resp);
    EXPECT_EQ(v["code"].asString(), code);
    EXPECT_EQ(v["status"].asInt(), status);
    EXPECT_TRUE(v["title"].isString());
    EXPECT_TRUE(v["type"].asString().ends_with(code));
  }

  drogon::HttpResponsePtr add(long long user, const std::string& number, const std::string& sku = "") {
    Json::Value b;
    b["number"] = number;
    if (!sku.empty()) {
      b["sku"] = sku;
    }
    return drogon::sync_wait(api.add_to_portfolio(json_as(user, b)));
  }
};

TEST_F(RestApiTest, RequiresValidInitData) {
  auto req = drogon::HttpRequest::newHttpRequest();
  expect_problem(drogon::sync_wait(api.me(req)), 401, "unauthorized");
  req->addHeader("X-Max-Init-Data", test::init_data_for("чужой-токен", kAlice, kAuthDate));
  expect_problem(drogon::sync_wait(api.me(req)), 401, "unauthorized");
  auto old = drogon::HttpRequest::newHttpRequest();
  old->addHeader("X-Max-Init-Data", test::init_data_for(kToken, kAlice, kAuthDate - (25LL * 3600)));
  expect_problem(drogon::sync_wait(api.me(old)), 401, "init_data_expired");
}

TEST_F(RestApiTest, MeAndDataStatus) {
  const auto me = drogon::sync_wait(api.me(as(kAlice)));
  ASSERT_EQ(me->getStatusCode(), drogon::k200OK);
  EXPECT_EQ(body(me)["max_user_id"].asInt64(), kAlice);
  EXPECT_EQ(body(me)["portfolio_count"].asInt(), 0);
  EXPECT_EQ(body(me)["demo_stage"].asString(), "base");
  const auto ds = drogon::sync_wait(api.data_status(as(kAlice)));
  ASSERT_EQ(ds->getStatusCode(), drogon::k200OK);
  EXPECT_EQ(body(ds)["source_date"].asString(), "2026-09-26");
  EXPECT_EQ(body(ds)["record_count"].asInt(), 3);
  EXPECT_TRUE(body(ds)["is_demo"].asBool());
  EXPECT_TRUE(body(ds)["next_update"].isNull());
}

TEST_F(RestApiTest, CheckByNumber) {
  auto req = as(kAlice);
  req->setParameter("number", "RU D-CR.PA08.B.89369/26");
  const auto resp = drogon::sync_wait(api.check(req));
  ASSERT_EQ(resp->getStatusCode(), drogon::k200OK);
  const auto v = body(resp);
  EXPECT_EQ(v["level"].asString(), "ok");
  EXPECT_EQ(v["number"].asString(), "RUD-CR.PA08.B.89369/26");
  EXPECT_EQ(v["display_number"].asString(), "RU Д-CR.PA08.B.89369/26");
  EXPECT_GT(v["check_id"].asInt64(), 0);
  EXPECT_EQ(v["card"]["status"].asString(), "active");
  EXPECT_EQ(v["card"]["status_name"].asString(), "действует");
  EXPECT_EQ(v["data_date"].asString(), "2026-09-26");
  ASSERT_GE(v["findings"].size(), 1U);
  EXPECT_EQ(v["findings"][0]["basis"].asString(), "fact");

  auto none = as(kAlice);
  expect_problem(drogon::sync_wait(api.check(none)), 400, "invalid_argument");
  auto bad = as(kAlice);
  bad->setParameter("number", "привет");
  expect_problem(drogon::sync_wait(api.check(bad)), 422, "number_not_recognized");
}

std::string multipart(const std::string& boundary, const std::string& content) {
  return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"doc.pdf\"\r\n" +
         "Content-Type: application/pdf\r\n\r\n" + content + "\r\n--" + boundary + "--\r\n";
}

TEST_F(RestApiTest, CheckFile) {
  auto req = as(kAlice, drogon::Post);
  req->addHeader("content-type", "multipart/form-data; boundary=XyZ");
  req->setBody(multipart("XyZ", "%PDF-1.7\nЕАЭС N RU D-CR.PA08.B.89369/26\n"));
  const auto resp = drogon::sync_wait(api.check_file(req));
  ASSERT_EQ(resp->getStatusCode(), drogon::k200OK) << resp->body();
  const auto v = body(resp);
  ASSERT_TRUE(v.isArray());
  ASSERT_EQ(v.size(), 1U);
  EXPECT_EQ(v[0]["level"].asString(), "ok");

  auto png = as(kAlice, drogon::Post);
  png->addHeader("content-type", "multipart/form-data; boundary=XyZ");
  png->setBody(multipart("XyZ", "just text"));
  expect_problem(drogon::sync_wait(api.check_file(png)), 415, "unsupported_media_type");

  auto not_multipart = as(kAlice, drogon::Post);
  not_multipart->setBody("raw");
  expect_problem(drogon::sync_wait(api.check_file(not_multipart)), 400, "invalid_argument");
}

TEST_F(RestApiTest, PortfolioLifecycleAndFilters) {
  const auto created = add(kAlice, "RU D-CR.PA08.B.89369/26", "SKU-1");
  ASSERT_EQ(created->getStatusCode(), drogon::k201Created) << created->body();
  EXPECT_EQ(body(created)["item"]["sku"].asString(), "SKU-1");
  EXPECT_EQ(body(created)["verdict"]["level"].asString(), "ok");
  expect_problem(add(kAlice, "RUD-CR.PA08.B.89369/26", "SKU-1"), 409, "conflict");
  ASSERT_EQ(add(kAlice, "RUC-RU.AB12.B.00017/24")->getStatusCode(), drogon::k201Created);
  ASSERT_EQ(add(kAlice, "RUD-RU.XY01.A.12345/21")->getStatusCode(), drogon::k201Created);

  auto list = as(kAlice);
  list->setParameter("limit", "2");
  const auto page1 = body(drogon::sync_wait(api.list_portfolio(list)));
  ASSERT_EQ(page1["items"].size(), 2U);
  ASSERT_TRUE(page1["next_cursor"].isString());
  auto next = as(kAlice);
  next->setParameter("cursor", page1["next_cursor"].asString());
  EXPECT_EQ(body(drogon::sync_wait(api.list_portfolio(next)))["items"].size(), 1U);

  auto terminated = as(kAlice);
  terminated->setParameter("status", "terminated");
  const auto t = body(drogon::sync_wait(api.list_portfolio(terminated)));
  ASSERT_EQ(t["items"].size(), 1U);
  EXPECT_EQ(t["items"][0]["doc_key"].asString(), "RUC-RU.AB12.B.00017/24");
  EXPECT_EQ(t["items"][0]["doc_kind"].asString(), "certificate");

  for (const auto& [param, value] : std::vector<std::pair<std::string, std::string>>{
           {"status", "действует"}, {"cursor", "x"}, {"limit", "0"}, {"limit", "101"}}) {
    auto bad = as(kAlice);
    bad->setParameter(param, value);
    expect_problem(drogon::sync_wait(api.list_portfolio(bad)), 400, "invalid_argument");
  }

  const auto id = body(created)["item"]["id"].asString();
  EXPECT_EQ(drogon::sync_wait(api.remove_from_portfolio(as(kAlice, drogon::Delete), id))->getStatusCode(),
            drogon::k204NoContent);
  expect_problem(drogon::sync_wait(api.remove_from_portfolio(as(kAlice, drogon::Delete), id)), 404,
                 "not_found");
  expect_problem(drogon::sync_wait(api.remove_from_portfolio(as(kAlice, drogon::Delete), "abc")), 404,
                 "not_found");
}

TEST_F(RestApiTest, ForeignItemIsNotFound) {
  const auto created = add(kAlice, "RU D-CR.PA08.B.89369/26");
  const auto id = body(created)["item"]["id"].asString();
  // IDOR (АРХ §10): чужой id → 404, и в списке Боба чужих записей нет.
  expect_problem(drogon::sync_wait(api.remove_from_portfolio(as(kBob, drogon::Delete), id)), 404,
                 "not_found");
  EXPECT_EQ(body(drogon::sync_wait(api.list_portfolio(as(kBob))))["items"].size(), 0U);
  EXPECT_EQ(body(drogon::sync_wait(api.list_portfolio(as(kAlice))))["items"].size(), 1U);
}

TEST_F(RestApiTest, AddValidatesBody) {
  auto no_json = as(kAlice, drogon::Post);
  no_json->setBody("number=1");
  expect_problem(drogon::sync_wait(api.add_to_portfolio(no_json)), 400, "invalid_argument");
  Json::Value bad_sku;
  bad_sku["number"] = "RU D-CR.PA08.B.89369/26";
  bad_sku["sku"] = 5;
  expect_problem(drogon::sync_wait(api.add_to_portfolio(json_as(kAlice, bad_sku))), 400, "invalid_argument");
  Json::Value with_inn;
  with_inn["number"] = "RU D-CR.PA08.B.89369/26";
  with_inn["supplier_inn"] = "7700000016";
  const auto r = drogon::sync_wait(api.add_to_portfolio(json_as(kAlice, with_inn)));
  ASSERT_EQ(r->getStatusCode(), drogon::k201Created);
  EXPECT_EQ(body(r)["item"]["supplier_inn"].asString(), "7700000016");
  expect_problem(add(kAlice, "не номер"), 422, "number_not_recognized");
}

/// Демо-пара для REST: в N+1 документ 89369/26 приостановлен.
std::shared_ptr<const fake::FakeSnapshot> updated_snapshot() {
  const auto base = fake::FakeSnapshot::three_records();
  std::vector<fake::FakeRecord> records;
  for (std::size_t i = 0; i < base->size(); ++i) {
    const auto r = base->record(i);
    records.push_back({.number = std::string{r.number}, .status = r.status, .expiry_date = r.expiry_date});
  }
  for (auto& r : records) {
    if (r.number == "RUD-CR.PA08.B.89369/26") {
      r.status = snapshot::Status::kSuspended;
    }
  }
  auto meta = base->meta();
  meta.version += 1;
  return std::make_shared<const fake::FakeSnapshot>(std::move(records), meta);
}

TEST(RestApiDemo, SimulateHistoryAndReset) {
  FakeDomainService domain{fake::FakeSnapshot::three_records(), updated_snapshot(),
                           year{2026} / month{9} / day{26}};
  RestApi api{domain, AuthConfig{.bot_token = std::string{kToken}, .now = test_now}};
  const auto as = [](long long user, drogon::HttpMethod method = drogon::Get) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(method);
    req->addHeader("X-Max-Init-Data", test::init_data_for(kToken, user, kAuthDate));
    return req;
  };
  Json::Value add;
  add["number"] = "RU D-CR.PA08.B.89369/26";
  auto add_req = drogon::HttpRequest::newHttpJsonRequest(add);
  add_req->setMethod(drogon::Post);
  add_req->addHeader("X-Max-Init-Data", test::init_data_for(kToken, kAlice, kAuthDate));
  ASSERT_EQ(drogon::sync_wait(api.add_to_portfolio(add_req))->getStatusCode(), drogon::k201Created);

  auto history_req = as(kAlice);
  history_req->setParameter("number", "RU D-CR.PA08.B.89369/26");
  auto h = RestApiTest::body(drogon::sync_wait(api.history(history_req)));
  EXPECT_EQ(h["doc_key"].asString(), "RUD-CR.PA08.B.89369/26");
  EXPECT_EQ(h["entries"].size(), 0U);  // стадия N

  const auto sim = drogon::sync_wait(api.simulate_update(as(kAlice, drogon::Post)));
  ASSERT_EQ(sim->getStatusCode(), drogon::k200OK);
  EXPECT_EQ(RestApiTest::body(sim)["notified"].asInt(), 1);
  const auto ds = RestApiTest::body(drogon::sync_wait(api.data_status(as(kAlice))));
  EXPECT_EQ(ds["demo_stage"].asString(), "updated");
  EXPECT_TRUE(ds["demo_update_available"].asBool());

  h = RestApiTest::body(drogon::sync_wait(api.history(history_req)));
  ASSERT_EQ(h["entries"].size(), 1U);
  EXPECT_EQ(h["entries"][0]["before"]["status"].asString(), "active");
  EXPECT_EQ(h["entries"][0]["after"]["status"].asString(), "suspended");
  EXPECT_EQ(h["entries"][0]["after"]["status_name"].asString(), "приостановлен");

  const auto reset = drogon::sync_wait(api.reset_demo(as(kAlice, drogon::Post)));
  EXPECT_EQ(reset->getStatusCode(), drogon::k204NoContent);
  EXPECT_EQ(RestApiTest::body(drogon::sync_wait(api.data_status(as(kAlice))))["demo_stage"].asString(),
            "base");

  // Ошибки: нет номера, мусор, без initData.
  RestApiTest::expect_problem(drogon::sync_wait(api.history(as(kAlice))), 400, "invalid_argument");
  auto garbage = as(kAlice);
  garbage->setParameter("number", "мусор");
  RestApiTest::expect_problem(drogon::sync_wait(api.history(garbage)), 422, "number_not_recognized");
  RestApiTest::expect_problem(drogon::sync_wait(api.simulate_update(drogon::HttpRequest::newHttpRequest())),
                              401, "unauthorized");
  RestApiTest::expect_problem(drogon::sync_wait(api.reset_demo(drogon::HttpRequest::newHttpRequest())), 401,
                              "unauthorized");
  RestApiTest::expect_problem(drogon::sync_wait(api.history(drogon::HttpRequest::newHttpRequest())), 401,
                              "unauthorized");
}

TEST(RestApiDevAuth, DevUserOnlyWithoutToken) {
  FakeDomainService domain{fake::FakeSnapshot::three_records(), fake::FakeSnapshot::three_records(),
                           year{2026} / month{9} / day{26}};
  const RestApi dev{domain, AuthConfig{.dev_user_id = 7}};
  const auto r = dev.authenticate(drogon::HttpRequest::newHttpRequest());
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value().max_user_id, 7);
  // С токеном dev-режим не действует даже при заданном dev_user_id.
  const RestApi prod{domain, AuthConfig{.bot_token = "t", .dev_user_id = 7}};
  EXPECT_FALSE(prod.authenticate(drogon::HttpRequest::newHttpRequest()).has_value());
}

}  // namespace
}  // namespace sk::certd
