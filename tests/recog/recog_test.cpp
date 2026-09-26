#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "sertkontrol_contracts.hpp"
#include "support/files.hpp"

namespace sk::recog {
namespace {

std::vector<std::byte> load(const std::string& name) {
  const auto s = test::read_file(std::filesystem::path{SK_FIXTURES_DIR} / "pdf" / name);
  std::vector<std::byte> out(s.size());
  std::ranges::transform(s, out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

std::vector<std::byte> bytes(std::string_view s) {
  std::vector<std::byte> out(s.size());
  std::ranges::transform(s, out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

// F2: «Выписка 89369/26 даёт верный номер и ссылку на реестр» (АРХ §2).
TEST(Recognize, ExtractGivesNumberAndRegistryLink) {
  const auto file = load("extract-89369-26.pdf");
  ASSERT_FALSE(file.empty());
  const auto started = std::chrono::steady_clock::now();
  const auto r = recognize(file, MediaType::kPdf, Limits{});
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  ASSERT_EQ(r.value().size(), 1U);
  const auto& f = r.value()[0];
  EXPECT_EQ(canon::canonicalize(f.raw), "RUD-CR.PA08.B.89369/26");
  EXPECT_EQ(f.source, Source::kTextLayer);
  EXPECT_EQ(f.registry_url, "https://pub.fsa.gov.ru/rds/declaration/view/21950326/common");
  // Бюджет потока A для PDF: QR ≤ 100 мс + текст ≤ 50 мс (АРХ §4); с запасом на санитайзеры.
  EXPECT_LT(elapsed, std::chrono::seconds{3});
}

TEST(Recognize, NumberOnlyInQr) {
  const auto r = recognize(load("qr-only.pdf"), MediaType::kPdf, Limits{});
  ASSERT_TRUE(r.has_value()) << r.error().detail;
  ASSERT_EQ(r.value().size(), 1U);
  EXPECT_EQ(r.value()[0].source, Source::kQr);
  EXPECT_EQ(canon::canonicalize(r.value()[0].raw), "RUD-RU.PA01.B.10001/25");
  EXPECT_FALSE(r.value()[0].registry_url.has_value());
}

TEST(Recognize, NoNumber) {
  const auto r = recognize(load("no-number.pdf"), MediaType::kPdf, Limits{});
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::kNumberNotRecognized);
}

TEST(Recognize, Limits) {
  const auto file = load("extract-89369-26.pdf");
  EXPECT_EQ(recognize(file, MediaType::kPdf, {.max_bytes = 1000}).error().code, ErrorCode::kFileTooLarge);
  EXPECT_EQ(recognize(file, MediaType::kPdf, {.max_pages = 1}).error().code, ErrorCode::kFileTooLarge);
  EXPECT_EQ(recognize(file, MediaType::kPdf, {.max_pixels = 1000}).error().code, ErrorCode::kFileTooLarge);
  EXPECT_EQ(recognize(file, MediaType::kPdf, {.timeout = std::chrono::milliseconds{-1}}).error().code,
            ErrorCode::kNumberNotRecognized);
}

TEST(Recognize, RejectsNonPdfAndPhotos) {
  EXPECT_EQ(recognize(bytes("просто текст RU D-RU.PA01.B.1/23"), MediaType::kPdf, {}).error().code,
            ErrorCode::kUnsupportedMediaType);
  EXPECT_EQ(recognize(bytes("%PDF-1.7 обрезанный файл"), MediaType::kPdf, {}).error().code,
            ErrorCode::kUnsupportedMediaType);
  EXPECT_EQ(recognize(bytes("\x89PNG"), MediaType::kPng, {}).error().code, ErrorCode::kUnsupportedMediaType);
  EXPECT_EQ(recognize(bytes("\xff\xd8\xff"), MediaType::kJpeg, {}).error().code,
            ErrorCode::kUnsupportedMediaType);
}

}  // namespace
}  // namespace sk::recog
