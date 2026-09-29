/// Исходящий HTTPS по-настоящему шифруется и проверяет сертификат (ADR-0014).
/// Сервер — `openssl s_server -www` с самоподписанным сертификатом во временном каталоге.
#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sertkontrol/maxapi/bot_api.hpp"
#include "support/pg.hpp"

namespace sk::maxapi {
namespace {

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

bool port_open(std::uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockaddr_in → sockaddr по POSIX.
  const bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
  ::close(fd);
  return ok;
}

class TlsTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    dir() = std::filesystem::temp_directory_path() / ("sk-tls-" + std::to_string(test::unique_id()));
    std::filesystem::create_directories(dir());
    const auto cmd =
        "openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost "
        "-addext subjectAltName=IP:127.0.0.1,DNS:localhost -keyout " +
        (dir() / "key.pem").string() + " -out " + (dir() / "cert.pem").string() + " >/dev/null 2>&1";
    // NOLINTNEXTLINE(cert-env33-c,concurrency-mt-unsafe): однократная подготовка фикстуры до запуска потоков.
    ASSERT_EQ(std::system(cmd.c_str()), 0);
    port() = free_port();
    const pid_t pid = ::fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
      const auto cert = (dir() / "cert.pem").string();
      const auto key = (dir() / "key.pem").string();
      const auto accept = std::to_string(port());
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): execlp — POSIX.
      ::execlp("openssl", "openssl", "s_server", "-quiet", "-www", "-accept", accept.c_str(), "-cert",
               cert.c_str(), "-key", key.c_str(), static_cast<char*>(nullptr));
      ::_exit(127);
    }
    server() = pid;
    for (int i = 0; i < 100 && !port_open(port()); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
  }
  static void TearDownTestSuite() {
    if (server() > 0) {
      ::kill(server(), SIGTERM);
      ::waitpid(server(), nullptr, 0);
    }
    std::filesystem::remove_all(dir());
  }

  static std::filesystem::path& dir() {
    static std::filesystem::path v;
    return v;
  }
  static std::uint16_t& port() {
    static std::uint16_t v = 0;
    return v;
  }
  static pid_t& server() {
    static pid_t v = 0;
    return v;
  }
  static std::string url() { return "https://127.0.0.1:" + std::to_string(port()) + "/"; }
};

TEST_F(TlsTest, CurlIsBuiltWithTls) {
  EXPECT_TRUE(HttpBotApi::tls_available());
}

TEST_F(TlsTest, DownloadOverRealTls) {
  HttpBotApi api{{.timeout_seconds = 10, .ca_file = (dir() / "cert.pem").string()}};
  const auto r = drogon::sync_wait(api.download(url(), 1'000'000));
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  EXPECT_GT(r.value().size(), 0U);  // страница статуса s_server пришла по TLS
}

TEST_F(TlsTest, UntrustedCertificateIsRejected) {
  // Системное хранилище не знает самоподписанный сертификат — соединение отвергается, данные не уходят.
  HttpBotApi api{{.timeout_seconds = 10}};
  const auto r = drogon::sync_wait(api.download(url(), 1'000'000));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::kInternal);
}

TEST_F(TlsTest, SizeLimitDuringTransfer) {
  HttpBotApi api{{.timeout_seconds = 10, .ca_file = (dir() / "cert.pem").string()}};
  const auto r = drogon::sync_wait(api.download(url(), 10));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::kFileTooLarge);
}

}  // namespace
}  // namespace sk::maxapi
