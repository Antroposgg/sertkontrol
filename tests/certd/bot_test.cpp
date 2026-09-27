#include "bot/bot.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "bot/card.hpp"
#include "fake_domain.hpp"
#include "memory_ports.hpp"
#include "sertkontrol/fakes.hpp"
#include "sertkontrol/maxapi/fake_bot_api.hpp"
#include "webhook.hpp"

namespace sk::certd::bot {
namespace {

using maxapi::Button;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;

constexpr std::int64_t kUser = 555;

class BotTest : public ::testing::Test {
 public:
  FakeDomainService domain{fake::FakeSnapshot::three_records(), fake::FakeSnapshot::three_records(),
                           year{2026} / month{9} / day{26}};
  MemoryOutbox outbox;
  MemoryDialogStore dialogs;
  maxapi::RecordingBotApi api;
  Bot bot{domain, outbox, dialogs, api, BotConfig{.max_file_bytes = 1000, .card = {.open_app = true}}};

  std::string handle(maxapi::Update u) { return drogon::sync_wait(bot.handle(std::move(u))); }
  std::string text(const std::string& t) {
    return handle(maxapi::MessageCreated{.mid = "m", .user_id = kUser, .text = t});
  }
  std::string press(const std::string& payload) {
    return handle(maxapi::MessageCallback{.callback_id = "cb", .payload = payload, .user_id = kUser});
  }
  void consent() {
    ASSERT_EQ(press("c:1"), "");
    outbox.clear();
  }
  [[nodiscard]] maxapi::OutgoingMessage last() const {
    const auto e = outbox.entries();
    return e.empty() ? maxapi::OutgoingMessage{} : e.back().msg;
  }
  static bool has_payload(const maxapi::OutgoingMessage& m, const std::string& prefix) {
    for (const auto& row : m.buttons) {
      for (const auto& b : row) {
        if (b.kind == Button::Kind::kCallback && b.payload.starts_with(prefix)) {
          return true;
        }
      }
    }
    return false;
  }
};

TEST_F(BotTest, StartAsksForConsent) {
  EXPECT_EQ(handle(maxapi::BotStarted{.user_id = kUser, .chat_id = 1}), "");
  const auto m = last();
  EXPECT_NE(m.text.find("Согласен"), std::string::npos);
  EXPECT_TRUE(has_payload(m, "c:1"));
  EXPECT_TRUE(has_payload(m, "h:1"));
}

TEST_F(BotTest, NoProcessingWithoutConsent) {
  text("RU D-CR.PA08.B.89369/26");
  EXPECT_TRUE(has_payload(last(), "c:1"));  // вместо карточки — запрос согласия
  press("w:1");
  ASSERT_FALSE(api.answers().empty());
  EXPECT_NE(api.answers().back().notification.find("согласие"), std::string::npos);
}

TEST_F(BotTest, ConsentThenHelp) {
  EXPECT_EQ(press("c:1"), "");
  EXPECT_EQ(api.answers().back().notification, "Спасибо!");
  EXPECT_NE(last().text.find("Как пользоваться"), std::string::npos);
  EXPECT_EQ(handle(maxapi::BotStarted{.user_id = kUser}), "");
  EXPECT_NE(last().text.find("Как пользоваться"), std::string::npos);  // повторный старт — справка
  press("h:1");
  EXPECT_NE(last().text.find("Как пользоваться"), std::string::npos);
}

// F3 в боте: карточка с блоками и метками, датой данных, пометкой тестовых данных и кнопками.
TEST_F(BotTest, TextGivesVerdictCard) {
  consent();
  EXPECT_EQ(text("Проверьте ЕАЭС N RU D-CR.PA08.B.89369/26"), "");
  const auto m = last();
  EXPECT_EQ(m.kind, maxapi::MessageKind::kVerdict);
  EXPECT_NE(m.text.find("<b>Декларация RU Д-CR.PA08.B.89369/26</b> — действует"), std::string::npos)
      << m.text;
  EXPECT_NE(m.text.find("<b>Факт</b>"), std::string::npos);
  EXPECT_NE(m.text.find("Заявитель: ООО «ТЕСТОВЫЙ ЗАЯВИТЕЛЬ», ИНН 7700000016"), std::string::npos);
  EXPECT_NE(m.text.find("Тестовые данные"), std::string::npos);
  EXPECT_NE(m.text.find("Данные реестра на 26.09.2026"), std::string::npos);
  EXPECT_TRUE(has_payload(m, "w:"));
  ASSERT_EQ(m.buttons.size(), 2U);
  EXPECT_EQ(m.buttons[1][0].kind, Button::Kind::kLink);
  EXPECT_EQ(m.buttons[1][1].kind, Button::Kind::kOpenApp);
  EXPECT_TRUE(maxapi::validate(m).has_value());
}

TEST_F(BotTest, ProblemCardHasCalculationAndRecommendation) {
  consent();
  text("RU D-RU.XY01.A.12345/21");  // срок истёк
  const auto m = last();
  EXPECT_NE(m.text.find("⛔"), std::string::npos);
  EXPECT_NE(m.text.find("срок истёк"), std::string::npos);
  EXPECT_NE(m.text.find("<b>Расчёт</b>"), std::string::npos);
  EXPECT_NE(m.text.find("<b>Рекомендация</b>"), std::string::npos);
}

TEST_F(BotTest, UnknownNumberAndGarbage) {
  consent();
  text("RU D-XX.0000.A.99999/26");
  EXPECT_NE(last().text.find("нет в данных"), std::string::npos);
  text("просто привет");
  EXPECT_NE(last().text.find("Не нашёл номер"), std::string::npos);
  handle(maxapi::MessageCreated{.mid = "m2", .user_id = kUser});
  EXPECT_NE(last().text.find("Как пользоваться"), std::string::npos);
}

TEST_F(BotTest, ManyNumbersGiveSummaryAndWatchAll) {
  consent();
  text("RU D-CR.PA08.B.89369/26\nRU C-RU.AB12.B.00017/24\nRU D-RU.XY01.A.12345/21\nRU D-XX.0000.A.99999/26");
  ASSERT_EQ(outbox.entries().size(), 1U);
  const auto m = last();
  EXPECT_NE(m.text.find("Проверено номеров: 4"), std::string::npos);
  EXPECT_NE(m.text.find("1 действуют, 0 с предупреждением, 2 не действуют, 1 нет в данных"),
            std::string::npos);
  ASSERT_TRUE(has_payload(m, "W:"));
  press(m.buttons[0][0].payload);
  EXPECT_EQ(api.answers().back().notification, "Добавлено: 4, уже было: 0");
  EXPECT_EQ(drogon::sync_wait(domain.me({.max_user_id = kUser})).value().portfolio_count, 4U);
}

TEST_F(BotTest, WatchAndUnwatch) {
  consent();
  text("RU D-CR.PA08.B.89369/26");
  const auto card = last();
  press(card.buttons[0][0].payload);
  EXPECT_EQ(api.answers().back().notification, "Добавлено на контроль");
  const auto w = last();
  EXPECT_NE(w.text.find("на контроле"), std::string::npos);
  ASSERT_TRUE(has_payload(w, "d:"));
  press(card.buttons[0][0].payload);
  EXPECT_EQ(api.answers().back().notification, "Уже на контроле");
  press(w.buttons[0][0].payload);
  EXPECT_EQ(api.answers().back().notification, "Снято с контроля");
  press(w.buttons[0][0].payload);
  EXPECT_EQ(api.answers().back().notification, "Уже снято");
  press("w:999999");
  EXPECT_EQ(api.answers().back().notification, "Не получилось добавить");
  press("W:999999");
  EXPECT_EQ(api.answers().back().notification, "Не получилось добавить");
  press("x:1");
  EXPECT_EQ(api.answers().back().notification, "Кнопка устарела");
}

std::vector<std::byte> bytes_of(std::string_view s) {
  std::vector<std::byte> out(s.size());
  std::ranges::transform(s, out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

TEST_F(BotTest, PdfAttachmentFlow) {
  consent();
  api.add_file("https://files/1", bytes_of("%PDF-1.7\nRU D-CR.PA08.B.89369/26\n"));
  const auto err = handle(maxapi::MessageCreated{
      .mid = "m3",
      .user_id = kUser,
      .attachments = {{.type = "file", .url = "https://files/1", .filename = "extract.pdf", .size = 40}}});
  EXPECT_EQ(err, "");
  const auto e = outbox.entries();
  ASSERT_EQ(e.size(), 2U);
  EXPECT_EQ(e[0].msg.text, "⏳ Проверяю документ…");
  EXPECT_GT(e[0].priority, e[1].priority);
  EXPECT_EQ(e[1].msg.kind, maxapi::MessageKind::kVerdict);
}

TEST_F(BotTest, AttachmentErrors) {
  consent();
  const auto send = [&](maxapi::Attachment a) {
    outbox.clear();
    return handle(maxapi::MessageCreated{.mid = "m", .user_id = kUser, .attachments = {std::move(a)}});
  };
  send({.type = "image", .url = "https://img"});
  EXPECT_NE(last().text.find("PDF"), std::string::npos);
  send({.type = "file", .url = "https://f", .filename = "scan.jpg", .size = 10});
  EXPECT_NE(last().text.find("PDF"), std::string::npos);
  send({.type = "file", .url = "https://f", .filename = "big.pdf", .size = 5000});
  EXPECT_NE(last().text.find("слишком большой"), std::string::npos);
  EXPECT_NE(send({.type = "file", .url = "https://missing", .filename = "a.pdf", .size = 10}), "");
  api.add_file("https://text", bytes_of("hello"));
  send({.type = "file", .url = "https://text", .filename = "a.pdf", .size = 10});
  EXPECT_NE(last().text.find("PDF"), std::string::npos);
  send({.type = "sticker"});
  EXPECT_NE(last().text.find("Как пользоваться"), std::string::npos);
}

TEST_F(BotTest, SupplierDialog) {
  consent();
  text("RU D-CR.PA08.B.89369/26");
  const auto card = last();
  ASSERT_TRUE(has_payload(card, "s:"));
  const auto supplier_button = card.buttons[0][1].payload;
  press(supplier_button);
  EXPECT_NE(last().text.find("ИНН поставщика"), std::string::npos);
  // Неверный ИНН — диалог продолжается.
  text("123");
  EXPECT_NE(last().text.find("контрольных цифр"), std::string::npos);
  text("7700000016");
  const auto done = last();
  EXPECT_NE(done.text.find("поставщик — ИНН 7700000016"), std::string::npos);
  ASSERT_TRUE(has_payload(done, "d:"));
  EXPECT_EQ(drogon::sync_wait(domain.me({.max_user_id = kUser})).value().portfolio_count, 1U);
  // Повторное указание поставщика у документа на контроле — та же запись, без дубля.
  press(supplier_button);
  text("7700000016");
  EXPECT_EQ(drogon::sync_wait(domain.me({.max_user_id = kUser})).value().portfolio_count, 1U);
  // Отмена.
  press(supplier_button);
  text("отмена");
  EXPECT_NE(last().text.find("не указываю"), std::string::npos);
  EXPECT_FALSE(drogon::sync_wait(dialogs.get(kUser)).value().has_value());
  // Номер документа вместо ИНН — диалог сброшен, идёт обычная проверка.
  press(supplier_button);
  text("RU C-RU.AB12.B.00017/24");
  EXPECT_EQ(last().kind, maxapi::MessageKind::kVerdict);
  EXPECT_FALSE(drogon::sync_wait(dialogs.get(kUser)).value().has_value());
  // Чужая проверка.
  press("s:999999");
  text("7700000016");
  EXPECT_NE(last().text.find("Не получилось"), std::string::npos);
}

TEST_F(BotTest, OtherUpdatesIgnored) {
  EXPECT_EQ(handle(maxapi::OtherUpdate{.type = "dialog_muted"}), "");
  EXPECT_TRUE(outbox.entries().empty());
}

TEST(Card, ChangeNoticeOneMessagePerUser) {
  const ChangeNotice n{.max_user_id = kUser,
                       .items = {{.item_id = 1,
                                  .doc_key = "RUD-CR.PA08.B.89369/26",
                                  .sku = "ЧАЙ-1",
                                  .before = snapshot::Status::kActive,
                                  .after = snapshot::Status::kSuspended,
                                  .status_date = year{2026} / month{9} / day{26},
                                  .suspended_until = year{2026} / month{12} / day{26}},
                                 {.item_id = 2,
                                  .doc_key = "RUD-CR.PA07.B.89369/26",
                                  .before = snapshot::Status::kUnknown,
                                  .after = snapshot::Status::kActive}},
                       .data_date = year{2026} / month{9} / day{26},
                       .is_demo = true};
  const auto msgs = render_change_notice(n, {.open_app = true});
  ASSERT_EQ(msgs.size(), 1U);
  const auto& m = msgs[0];
  EXPECT_EQ(m.kind, maxapi::MessageKind::kStatusChanged);
  EXPECT_NE(m.text.find("<b>приостановлен</b> до 26.12.2026 с 26.09.2026 (было: действует), SKU ЧАЙ-1"),
            std::string::npos);
  EXPECT_NE(m.text.find("(раньше в данных не было)"), std::string::npos);
  EXPECT_NE(m.text.find("Данные реестра на 26.09.2026"), std::string::npos);
  EXPECT_NE(m.text.find("Тестовые данные"), std::string::npos);
  ASSERT_EQ(m.buttons.size(), 1U);
  EXPECT_EQ(m.buttons[0][0].kind, Button::Kind::kOpenApp);
  EXPECT_TRUE(maxapi::validate(m).has_value());
  EXPECT_TRUE(render_change_notice({.max_user_id = kUser}, {}).empty());
}

TEST(Card, LongChangeNoticeSplitsByDocuments) {
  ChangeNotice n{.max_user_id = kUser, .data_date = year{2026} / month{9} / day{26}};
  for (int i = 0; i < 120; ++i) {
    n.items.push_back({.item_id = i + 1,
                       .doc_key = "RUD-CN.PA01.B." + std::to_string(10000 + i) + "/25",
                       .sku = "ДЛИННЫЙ-АРТИКУЛ-" + std::to_string(i),
                       .before = snapshot::Status::kActive,
                       .after = snapshot::Status::kTerminated});
  }
  const auto msgs = render_change_notice(n, {});
  ASSERT_GT(msgs.size(), 1U);
  std::size_t lines = 0;
  for (const auto& m : msgs) {
    EXPECT_TRUE(maxapi::validate(m).has_value()) << maxapi::utf8_length(m.text);
    EXPECT_LE(maxapi::utf8_length(m.text), maxapi::kMaxTextLength);
    EXPECT_NE(m.text.find("Данные реестра на"), std::string::npos);  // каждое сообщение самодостаточно
    for (std::size_t pos = m.text.find("• "); pos != std::string::npos; pos = m.text.find("• ", pos + 1)) {
      ++lines;
    }
  }
  EXPECT_EQ(lines, 120U);  // ни одна строка не потеряна и не порвана
}

TEST(Card, ParseCallback) {
  EXPECT_EQ(parse_callback("w:17").value_or(Callback{}).arg, 17);
  EXPECT_EQ(parse_callback("s:5").value_or(Callback{}).action, 's');
  EXPECT_EQ(parse_callback("W:3").value_or(Callback{}).action, 'W');
  for (const char* bad : {"", "w", "w:", "w:x", "w:-1", "w:0", "q:1", "w1", "w:1x"}) {
    EXPECT_FALSE(parse_callback(bad).has_value()) << bad;
  }
}

TEST(Card, SummaryOfTwentyFitsLimits) {
  CheckResult r{.batch_id = 1};
  for (int i = 0; i < 20; ++i) {
    const verify::Verdict v{.query = "q",
                            .number = "RUD-RU.PA01.B." + std::to_string(10000 + i) + "/25",
                            .level = verify::Level::kOk};
    r.verdicts.push_back({.check_id = i + 1, .verdict = v});
  }
  const auto m = summary(1, r, {.open_app = true});
  EXPECT_TRUE(maxapi::validate(m).has_value());
  EXPECT_LE(maxapi::utf8_length(m.text), maxapi::kMaxTextLength);
}

TEST(Card, EscapesHtmlInData) {
  const CheckedVerdict cv{.check_id = 1,
                          .verdict = {.number = "RUD-A.B.1/26",
                                      .level = verify::Level::kOk,
                                      .card = verify::Card{.applicant_name = "ООО <b>\"Х&Y\"</b>"}}};
  const auto m = verdict_card(1, cv, {});
  EXPECT_NE(m.text.find("ООО &lt;b&gt;&quot;Х&amp;Y&quot;&lt;/b&gt;"), std::string::npos);
  EXPECT_EQ(m.buttons.size(), 1U);  // без ссылки https и без open_app
}

TEST(Card, ErrorMessages) {
  for (const auto code :
       {ErrorCode::kNumberNotRecognized, ErrorCode::kUnsupportedMediaType, ErrorCode::kFileTooLarge,
        ErrorCode::kRateLimited, ErrorCode::kSnapshotUnavailable, ErrorCode::kInternal}) {
    const auto m = error_message(1, Error{code, "x"});
    EXPECT_TRUE(maxapi::validate(m).has_value());
  }
}

// ── Webhook ──

class WebhookTest : public BotTest {
 public:
  MemoryInboundLog inbound;
  Webhook hook{bot, inbound, "secret-12345"};

  drogon::HttpResponsePtr post(const std::string& body, const std::string& secret = "secret-12345") {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->addHeader("X-Max-Bot-Api-Secret", secret);
    req->setBody(body);
    return drogon::sync_wait(hook.handle(req));
  }
};

TEST_F(WebhookTest, SecretParsingAndDedup) {
  EXPECT_EQ(post("{}", "wrong")->getStatusCode(), drogon::k401Unauthorized);
  EXPECT_EQ(post("not json")->getStatusCode(), drogon::k400BadRequest);
  const std::string started =
      R"({"update_type":"bot_started","timestamp":1,"chat_id":1,"user":{"user_id":555}})";
  EXPECT_EQ(post(started)->getStatusCode(), drogon::k200OK);
  EXPECT_EQ(post(started)->getStatusCode(), drogon::k200OK);  // повтор MAX
  EXPECT_EQ(outbox.entries().size(), 1U);  // обработано ровно один раз
  EXPECT_EQ(inbound.processed().size(), 1U);
  EXPECT_EQ(inbound.processed().begin()->second, "");
}

}  // namespace
}  // namespace sk::certd::bot
