#include "sertkontrol/verify/text.hpp"

#include <gtest/gtest.h>

#include "support/checked.hpp"

namespace sk::verify {
namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

TEST(VerifyText, FormatDate) {
  EXPECT_EQ(format_date(year{2026} / month{1} / day{5}), "05.01.2026");
  EXPECT_EQ(format_date(year{2031} / month{12} / day{31}), "31.12.2031");
}

TEST(VerifyText, Names) {
  EXPECT_EQ(status_name(snapshot::Status::kActive), "действует");
  EXPECT_EQ(status_name(snapshot::Status::kSuspended), "приостановлен");
  EXPECT_EQ(status_name(snapshot::Status::kTerminated), "прекращён");
  EXPECT_EQ(status_name(snapshot::Status::kAnnulled), "аннулирован");
  EXPECT_EQ(status_name(snapshot::Status::kArchived), "архивный");
  EXPECT_EQ(status_name(snapshot::Status::kUnknown), "не распознан");
  EXPECT_EQ(kind_name(canon::DocKind::kDeclaration), "Декларация");
  EXPECT_EQ(kind_name(canon::DocKind::kCertificate), "Сертификат");
}

TEST(VerifyText, DisplayNumber) {
  EXPECT_EQ(display_number("RUD-CR.PA08.B.89369/26"), "RU Д-CR.PA08.B.89369/26");
  EXPECT_EQ(display_number("RUC-RU.AЯ46.B.00017/24"), "RU С-RU.AЯ46.B.00017/24");
  EXPECT_EQ(display_number("XYZ"), "XYZ");
}

TEST(VerifyText, RegistryUrl) {
  EXPECT_EQ(registry_url(canon::DocKind::kDeclaration, 1),
            "https://pub.fsa.gov.ru/rds/declaration/view/1/common");
  EXPECT_EQ(registry_url(canon::DocKind::kCertificate, 2),
            "https://pub.fsa.gov.ru/rss/certificate/view/2/common");
  EXPECT_EQ(registry_url(canon::DocKind::kDeclaration, 0), "https://pub.fsa.gov.ru/rds/declaration");
}

// QR выписки ФСА — ссылка на запись реестра, а не номер (docs/plan.md §8.1).
TEST(VerifyText, ParseRegistryUrl) {
  const auto d = parse_registry_url("https://pub.fsa.gov.ru/rds/declaration/view/21950326/common");
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(sk::test::checked(d).kind, canon::DocKind::kDeclaration);
  EXPECT_EQ(sk::test::checked(d).registry_id, 21950326U);
  const auto c = parse_registry_url("Выписка: http://pub.fsa.gov.ru/rss/certificate/view/42 — проверьте");
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(sk::test::checked(c).kind, canon::DocKind::kCertificate);
  EXPECT_EQ(sk::test::checked(c).registry_id, 42U);
  EXPECT_EQ(sk::test::checked(parse_registry_url("pub.fsa.gov.ru/rds/declaration/view/18446744073709551615"))
                .registry_id,
            18446744073709551615ULL);
  for (const auto* bad :
       {"", "https://pub.fsa.gov.ru/rds/declaration", "pub.fsa.gov.ru/rds/declaration/view/",
        "pub.fsa.gov.ru/rds/declaration/view/0/common", "pub.fsa.gov.ru/rds/declaration/view/x1",
        "pub.fsa.gov.ru/rds/declaration/view/18446744073709551616",
        "https://example.com/rds/declaration/view/1", "RU D-CR.PA08.B.89369/26"}) {
    EXPECT_FALSE(parse_registry_url(bad).has_value()) << bad;
  }
}

}  // namespace
}  // namespace sk::verify
