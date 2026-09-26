#include <drogon/drogon.h>
#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "sertkontrol/maxapi/bot_api.hpp"

namespace sk::maxapi {
namespace {

/// Что увидел локальный «MAX»: проверяем реальную сериализацию запросов Drogon-клиента.
struct Seen {
  std::mutex mutex;
  std::string path;
  std::string query;
  std::string authorization;
  std::string body;
};

Seen& seen() {
  static Seen s;
  return s;
}

std::uint16_t free_port() {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockaddr_in → sockaddr по POSIX.
  auto* sa = reinterpret_cast<sockaddr*>(&addr);
  socklen_t len = sizeof(addr);
  if (::bind(fd, sa, len) != 0 || ::getsockname(fd, sa, &len) != 0) {
    ::close(fd);
    return 0;
  }
  ::close(fd);
  return ntohs(addr.sin_port);
}

class HttpBotApiTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    server_port = free_port();
    auto record = [](const drogon::HttpRequestPtr& req) {
      const std::scoped_lock lock(seen().mutex);
      seen().path = req->path();
      seen().query = req->query();
      seen().authorization = req->getHeader("Authorization");
      seen().body = std::string{req->body()};
    };
    auto& app = drogon::app();
    app.registerHandler("/messages",
                        [record](const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb_in) {
                          const auto cb = std::move(cb_in);
                          record(req);
                          auto resp = drogon::HttpResponse::newHttpResponse();
                          if (req->getParameter("user_id") == "429") {
                            resp->setStatusCode(drogon::k429TooManyRequests);
                          } else if (req->getParameter("user_id") == "500") {
                            resp->setStatusCode(drogon::k500InternalServerError);
                          } else if (req->getParameter("user_id") == "401") {
                            resp->setStatusCode(drogon::k401Unauthorized);
                          } else if (req->getParameter("user_id") == "400") {
                            resp->setStatusCode(drogon::k400BadRequest);
                          } else {
                            resp->setBody(R"({"message":{}})");
                          }
                          cb(resp);
                        },
                        {drogon::Post});
    app.registerHandler("/answers",
                        [record](const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb_in) {
                          const auto cb = std::move(cb_in);
                          record(req);
                          Json::Value v;
                          v["success"] = req->getParameter("callback_id") != "bad";
                          v["message"] = "callback expired";
                          cb(drogon::HttpResponse::newHttpJsonResponse(v));
                        },
                        {drogon::Post});
    app.registerHandler(
        "/files/{1}",
        [](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& cb_in,
           const std::string& name) {
          const auto cb = std::move(cb_in);
          auto resp = drogon::HttpResponse::newHttpResponse();
          if (name == "missing") {
            resp->setStatusCode(drogon::k404NotFound);
          } else {
            resp->setBody(std::string(100, 'x'));
          }
          cb(resp);
        },
        {drogon::Get});
    app.setThreadNum(1).setLogLevel(trantor::Logger::kWarn).addListener("127.0.0.1", server_port);
    server_thread = std::thread([] { drogon::app().run(); });
    for (int i = 0; i < 200 && !drogon::app().isRunning(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
  }
  static void TearDownTestSuite() {
    drogon::app().quit();
    server_thread.join();
  }

  static HttpBotApi api() {
    return HttpBotApi{{.base_url = "http://127.0.0.1:" + std::to_string(server_port),
                       .token = "tok-123",
                       .bot_username = "sertkontrol_bot",
                       .timeout_seconds = 5,
                       .allow_http_downloads = true}};
  }
  static std::string base() { return "http://127.0.0.1:" + std::to_string(server_port); }

  static inline std::uint16_t server_port{0};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
  static inline std::thread server_thread;     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};

TEST_F(HttpBotApiTest, SendMessageUsesHeaderQueryAndBody) {
  auto client = api();
  const auto r =
      drogon::sync_wait(client.send_message({.max_user_id = 67890,
                                             .text = "<b>Тест</b>",
                                             .buttons = {{{.text = "На контроль", .payload = "w:1"}}}}));
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  const std::scoped_lock lock(seen().mutex);
  EXPECT_EQ(seen().path, "/messages");
  EXPECT_EQ(seen().query, "user_id=67890");
  EXPECT_EQ(seen().authorization, "tok-123");
  EXPECT_NE(seen().body.find(R"("format":"html")"), std::string::npos);
  EXPECT_NE(seen().body.find(R"("payload":"w:1")"), std::string::npos);
}

TEST_F(HttpBotApiTest, StatusMapping) {
  auto client = api();
  const auto code = [&](std::int64_t user) {
    const auto r = drogon::sync_wait(client.send_message({.max_user_id = user, .text = "x"}));
    return r ? ErrorCode::kInternal : r.error().code;
  };
  EXPECT_EQ(code(429), ErrorCode::kRateLimited);
  EXPECT_EQ(code(500), ErrorCode::kInternal);
  EXPECT_EQ(code(401), ErrorCode::kUnauthorized);
  EXPECT_EQ(code(400), ErrorCode::kInvalidArgument);
  // Невалидное сообщение не отправляется.
  EXPECT_EQ(drogon::sync_wait(client.send_message({.max_user_id = 1, .text = ""})).error().code,
            ErrorCode::kInvalidArgument);
}

TEST_F(HttpBotApiTest, AnswerCallback) {
  auto client = api();
  ASSERT_TRUE(drogon::sync_wait(client.answer_callback("cb/1 2", "Добавлено на контроль")));
  {
    const std::scoped_lock lock(seen().mutex);
    EXPECT_EQ(seen().query, "callback_id=cb%2F1%202");
    EXPECT_NE(seen().body.find("Добавлено на контроль"), std::string::npos);
  }
  EXPECT_EQ(drogon::sync_wait(client.answer_callback("bad", "x")).error().code, ErrorCode::kInvalidArgument);
}

TEST_F(HttpBotApiTest, Download) {
  auto client = api();
  const auto ok = drogon::sync_wait(client.download(base() + "/files/extract.pdf", 1000));
  ASSERT_TRUE(ok.has_value()) << ok.error().detail;
  EXPECT_EQ(ok.value().size(), 100U);
  EXPECT_EQ(drogon::sync_wait(client.download(base() + "/files/extract.pdf", 10)).error().code,
            ErrorCode::kFileTooLarge);
  EXPECT_EQ(drogon::sync_wait(client.download(base() + "/files/missing", 1000)).error().code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(drogon::sync_wait(client.download("ftp://x/y", 1000)).error().code, ErrorCode::kInvalidArgument);
  HttpBotApi strict{{.base_url = base()}};
  EXPECT_EQ(drogon::sync_wait(strict.download(base() + "/files/a", 1000)).error().code,
            ErrorCode::kInvalidArgument);
}

TEST_F(HttpBotApiTest, UnreachableServer) {
  HttpBotApi dead{{.base_url = "http://127.0.0.1:1", .timeout_seconds = 2}};
  EXPECT_EQ(drogon::sync_wait(dead.send_message({.max_user_id = 1, .text = "x"})).error().code,
            ErrorCode::kInternal);
}

TEST(SplitUrl, Cases) {
  const auto a = split_url("https://files.max.ru/a/b?x=1").value_or(SplitUrl{});
  EXPECT_EQ(a.origin, "https://files.max.ru");
  EXPECT_EQ(a.path, "/a/b?x=1");
  EXPECT_EQ(split_url("http://h:8080").value_or(SplitUrl{}).path, "/");
  EXPECT_FALSE(split_url("files.max.ru/a").has_value());
  EXPECT_FALSE(split_url("file:///etc/passwd").has_value());
  EXPECT_FALSE(split_url("https:///x").has_value());
  EXPECT_FALSE(split_url("https://user@evil/x").has_value());
}

}  // namespace
}  // namespace sk::maxapi
