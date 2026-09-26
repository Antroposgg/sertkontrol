#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "clock.hpp"
#include "json_views.hpp"
#include "media.hpp"
#include "rate_limiter.hpp"
#include "recognition_pool.hpp"

namespace sk::certd {
namespace {

using namespace std::chrono_literals;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

std::vector<std::byte> bytes_of(std::string_view s) {
  std::vector<std::byte> out(s.size());
  std::ranges::transform(s, out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

TEST(MoscowDate, UtcPlusThree) {
  // 2026-09-25 21:30 UTC = 2026-09-26 00:30 МСК.
  const auto t = std::chrono::sys_days{year{2026} / month{9} / day{25}} + 21h + 30min;
  EXPECT_EQ(moscow_date(t), (year{2026} / month{9} / day{26}));
  EXPECT_EQ(moscow_date(t - 1h), (year{2026} / month{9} / day{25}));
}

TEST(Media, DetectBySignature) {
  EXPECT_EQ(detect_media_type(bytes_of("%PDF-1.7")), recog::MediaType::kPdf);
  EXPECT_EQ(detect_media_type(bytes_of("garbage\n%PDF-1.4")), recog::MediaType::kPdf);
  EXPECT_EQ(detect_media_type(bytes_of("\xFF\xD8\xFF\xE0")), recog::MediaType::kJpeg);
  EXPECT_EQ(detect_media_type(bytes_of("\x89PNG\r\n")), recog::MediaType::kPng);
  EXPECT_EQ(detect_media_type(bytes_of("PK\x03\x04")), std::nullopt);
  EXPECT_EQ(detect_media_type({}), std::nullopt);
}

TEST(RateLimiter, ThirtyPerMinutePerUser) {
  RateLimiter limiter;
  const auto t0 = RateLimiter::Clock::time_point{};
  for (int i = 0; i < 30; ++i) {
    EXPECT_TRUE(limiter.try_acquire(1, t0)) << i;
  }
  EXPECT_FALSE(limiter.try_acquire(1, t0));
  EXPECT_TRUE(limiter.try_acquire(2, t0));       // другой пользователь независим
  EXPECT_TRUE(limiter.try_acquire(1, t0 + 2s));  // 30/мин = 1 токен за 2 с
  EXPECT_FALSE(limiter.try_acquire(1, t0 + 2s));
  EXPECT_TRUE(limiter.try_acquire(1, t0 + 10min));
}

TEST(RecognitionPool, RunsInPoolAndLimitsQueue) {
  std::mutex m;
  std::condition_variable cv;
  bool release = false;
  std::atomic<int> started{0};
  std::thread::id worker;
  RecognitionPool pool{
      1, 1, [&](std::span<const std::byte>, recog::MediaType, const recog::Limits&) {
        worker = std::this_thread::get_id();
        ++started;
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return release; });
        return Result<std::vector<recog::Found>>{std::vector<recog::Found>{{.raw = "RU D-A.B.1/26"}}};
      }};
  std::optional<Result<std::vector<recog::Found>>> first;
  std::thread t([&] { first = drogon::sync_wait(pool.recognize({}, recog::MediaType::kPdf)); });
  while (started.load() == 0) {
    std::this_thread::sleep_for(1ms);
  }
  // Очередь из одного места занята — вторая задача получает отказ сразу.
  const auto second = drogon::sync_wait(pool.recognize({}, recog::MediaType::kPdf));
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code, ErrorCode::kRateLimited);
  {
    const std::scoped_lock lock(m);
    release = true;
  }
  cv.notify_all();
  t.join();
  ASSERT_TRUE(first.has_value() && first->has_value());
  EXPECT_NE(worker, std::this_thread::get_id());
}

TEST(JsonViews, HttpStatusForEveryCode) {
  const std::vector<std::pair<ErrorCode, int>> expected = {{ErrorCode::kInvalidArgument, 400},
                                                           {ErrorCode::kUnauthorized, 401},
                                                           {ErrorCode::kInitDataExpired, 401},
                                                           {ErrorCode::kForbidden, 403},
                                                           {ErrorCode::kNotFound, 404},
                                                           {ErrorCode::kNotFoundInSnapshot, 404},
                                                           {ErrorCode::kConflict, 409},
                                                           {ErrorCode::kFileTooLarge, 413},
                                                           {ErrorCode::kUnsupportedMediaType, 415},
                                                           {ErrorCode::kNumberNotRecognized, 422},
                                                           {ErrorCode::kRateLimited, 429},
                                                           {ErrorCode::kSnapshotUnavailable, 503},
                                                           {ErrorCode::kInternal, 500}};
  for (const auto& [code, status] : expected) {
    EXPECT_EQ(http_status(code), status) << to_string(code);
    const auto p = problem_json(Error{code, "подробности"});
    EXPECT_EQ(p["status"].asInt(), status);
    EXPECT_EQ(p["code"].asString(), std::string{to_string(code)});
    EXPECT_EQ(p["detail"].asString(), "подробности");
    EXPECT_FALSE(p["title"].asString().empty());
  }
}

TEST(JsonViews, VerdictShape) {
  verify::Verdict v{
      .query = "q",
      .number = "",
      .level = verify::Level::kNotFound,
      .findings = {{.basis = verify::Basis::kCalculation, .rule = "number.unparsed", .text = "t"}},
      .suggestions = {{.number = "RUD-A.B.1/26", .distance = 1}},
      .data_date = year{2026} / month{9} / day{5}};
  const auto j = to_json(v);
  EXPECT_TRUE(j["number"].isNull());
  EXPECT_TRUE(j["card"].isNull());
  EXPECT_EQ(j["level"].asString(), "not_found");
  EXPECT_EQ(j["data_date"].asString(), "2026-09-05");
  EXPECT_EQ(j["findings"][0]["basis"].asString(), "calculation");
  EXPECT_EQ(j["suggestions"][0]["display_number"].asString(), "RU Д-A.B.1/26");
  for (const auto l : {verify::Level::kOk, verify::Level::kWarning, verify::Level::kProblem,
                       verify::Level::kNeedsConfirmation}) {
    v.level = l;
    EXPECT_FALSE(to_json(v)["level"].asString().empty());
  }
  v.findings = {{.basis = verify::Basis::kFact}, {.basis = verify::Basis::kRecommendation}};
  EXPECT_EQ(to_json(v)["findings"][1]["basis"].asString(), "recommendation");
  EXPECT_EQ(to_json(DataStatus{.source_date = year{2026} / month{1} / day{2}})["source_date"].asString(),
            "2026-01-02");
  EXPECT_EQ(to_json(Me{.demo_stage = DemoStage::kUpdated})["demo_stage"].asString(), "updated");
  EXPECT_TRUE(to_json(PortfolioItem{})["sku"].isNull());
}

}  // namespace
}  // namespace sk::certd
