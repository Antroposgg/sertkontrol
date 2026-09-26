#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include <json/json.h>

#include "sertkontrol/maxapi/message.hpp"
#include "sertkontrol/maxapi/update.hpp"
#include "support/files.hpp"

namespace sk::maxapi {
namespace {

std::string fixture(const std::string& name) {
  return test::read_file(std::filesystem::path{SK_FIXTURES_DIR} / "max" / name);
}

Json::Value parse_json(const std::string& s) {
  Json::Value v;
  const Json::CharReaderBuilder b;
  const std::unique_ptr<Json::CharReader> r{b.newCharReader()};
  std::string errs;
  EXPECT_TRUE(r->parse(s.data(), s.data() + s.size(), &v, &errs)) << errs;
  return v;
}

TEST(Update, MessageCreatedWithPdf) {
  const auto u = parse_update(fixture("message_created_pdf.json"));
  ASSERT_TRUE(u.has_value()) << u.error().detail;
  const auto& m = std::get<MessageCreated>(u.value());
  EXPECT_EQ(m.mid, "mid.0000000000000001");
  EXPECT_EQ(m.user_id, 67890);
  EXPECT_EQ(m.chat_id, 555);
  EXPECT_TRUE(m.text.empty());
  ASSERT_EQ(m.attachments.size(), 1U);
  EXPECT_EQ(m.attachments[0].type, "file");
  EXPECT_EQ(m.attachments[0].url, "https://files.example/abc");
  EXPECT_EQ(m.attachments[0].filename, "extract.pdf");
  EXPECT_EQ(m.attachments[0].size, 37786U);
  EXPECT_EQ(dedup_key(u.value()), "m:mid.0000000000000001");
}

TEST(Update, CallbackAndBotStarted) {
  const auto cb = parse_update(fixture("message_callback.json"));
  ASSERT_TRUE(cb.has_value());
  const auto& c = std::get<MessageCallback>(cb.value());
  EXPECT_EQ(c.callback_id, "cb-42");
  EXPECT_EQ(c.payload, "w:17");
  EXPECT_EQ(c.user_id, 67890);
  EXPECT_EQ(dedup_key(cb.value()), "c:cb-42");

  const auto bs = parse_update(fixture("bot_started.json"));
  ASSERT_TRUE(bs.has_value());
  const auto& b = std::get<BotStarted>(bs.value());
  EXPECT_EQ(b.user_id, 67890);
  EXPECT_EQ(b.chat_id, 555);
  EXPECT_EQ(b.payload, "promo");
  EXPECT_EQ(dedup_key(bs.value()), "s:67890:1790000002000");
}

TEST(Update, OtherAndInvalid) {
  const auto o = parse_update(R"({"update_type":"dialog_muted","timestamp":5})");
  ASSERT_TRUE(o.has_value());
  EXPECT_EQ(std::get<OtherUpdate>(o.value()).type, "dialog_muted");
  EXPECT_EQ(dedup_key(o.value()), "o:dialog_muted:5");
  for (const char* bad :
       {"", "[]", "{}", R"({"update_type":"message_created"})",
        R"({"update_type":"message_callback","callback":{}})", R"({"update_type":"bot_started"})"}) {
    EXPECT_FALSE(parse_update(bad).has_value()) << bad;
  }
}

OutgoingMessage card() {
  return {.max_user_id = 67890,
          .text = "<b>Декларация</b> действует",
          .kind = MessageKind::kVerdict,
          .is_demo = true,
          .buttons = {{{.kind = Button::Kind::kCallback, .text = "На контроль", .payload = "w:17"}},
                      {{.kind = Button::Kind::kLink,
                        .text = "Открыть в реестре",
                        .url = "https://pub.fsa.gov.ru/rds/declaration"},
                       {.kind = Button::Kind::kOpenApp, .text = "Подробнее", .payload = "doc-17"}}}};
}

TEST(Message, OutboxRoundTrip) {
  const auto json = to_outbox_json(card());
  const auto v = parse_json(json);
  EXPECT_EQ(v["version"].asInt(), 1);
  EXPECT_EQ(v["kind"].asString(), "verdict");
  EXPECT_EQ(v["buttons"][1][1]["start_param"].asString(), "doc-17");
  const auto back = from_outbox_json(json);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back.value().text, card().text);
  EXPECT_EQ(back.value().kind, MessageKind::kVerdict);
  EXPECT_TRUE(back.value().is_demo);
  ASSERT_EQ(back.value().buttons.size(), 2U);
  EXPECT_EQ(back.value().buttons[1][0].url, "https://pub.fsa.gov.ru/rds/declaration");
  EXPECT_EQ(back.value().buttons[1][1].kind, Button::Kind::kOpenApp);
  EXPECT_FALSE(from_outbox_json(R"({"version":2})").has_value());
  EXPECT_FALSE(from_outbox_json(
                   R"({"version":1,"max_user_id":1,"text":"x","buttons":[[{"type":"magic","text":"x"}]]})")
                   .has_value());
}

TEST(Message, NewMessageBodyMatchesMaxSchema) {
  const auto v = parse_json(to_new_message_body(card(), "sertkontrol_bot"));
  EXPECT_EQ(v["format"].asString(), "html");
  const auto& kb = v["attachments"][0];
  EXPECT_EQ(kb["type"].asString(), "inline_keyboard");
  const auto& rows = kb["payload"]["buttons"];
  EXPECT_EQ(rows[0][0]["type"].asString(), "callback");
  EXPECT_EQ(rows[0][0]["payload"].asString(), "w:17");
  EXPECT_EQ(rows[1][0]["type"].asString(), "link");
  EXPECT_EQ(rows[1][1]["type"].asString(), "open_app");
  EXPECT_EQ(rows[1][1]["web_app"].asString(), "sertkontrol_bot");
  EXPECT_EQ(rows[1][1]["payload"].asString(), "doc-17");
  // Без имени бота open_app опускается, ряд с одной ссылкой остаётся.
  const auto no_app = parse_json(to_new_message_body(card(), ""));
  EXPECT_EQ(no_app["attachments"][0]["payload"]["buttons"][1].size(), 1U);
  const OutgoingMessage plain{.max_user_id = 1, .text = "x"};
  EXPECT_FALSE(parse_json(to_new_message_body(plain, "b")).isMember("attachments"));
}

TEST(Message, ValidateLimits) {
  EXPECT_TRUE(validate(card()).has_value());
  auto m = card();
  m.max_user_id = 0;
  EXPECT_FALSE(validate(m).has_value());
  m = card();
  m.text = std::string(4001, 'x');
  EXPECT_FALSE(validate(m).has_value());
  m.text.clear();
  for (int i = 0; i < 4000; ++i) {
    m.text += "ж";  // 4000 символов по 2 байта — допустимо
  }
  EXPECT_TRUE(validate(m).has_value());
  m = card();
  m.buttons.assign(31, {{.text = "a", .payload = "w:1"}});
  EXPECT_FALSE(validate(m).has_value());
  m = card();
  m.buttons = {std::vector<Button>(8, {.text = "a", .payload = "w:1"})};
  EXPECT_FALSE(validate(m).has_value());
  m.buttons = {std::vector<Button>(4, {.kind = Button::Kind::kLink, .text = "a", .url = "https://x"})};
  EXPECT_FALSE(validate(m).has_value());
  m.buttons = {{{.kind = Button::Kind::kLink, .text = "a", .url = "http://x"}}};
  EXPECT_FALSE(validate(m).has_value());
  m.buttons = {{{.kind = Button::Kind::kOpenApp, .text = "a", .payload = "doc 1"}}};
  EXPECT_FALSE(validate(m).has_value());
  m.buttons = {{{.text = "a", .payload = std::string(1025, 'x')}}};
  EXPECT_FALSE(validate(m).has_value());
  m.buttons = {{{.text = "", .payload = "w:1"}}};
  EXPECT_FALSE(validate(m).has_value());
  m.buttons = {{}};
  EXPECT_FALSE(validate(m).has_value());
}

TEST(Message, HtmlEscape) {
  EXPECT_EQ(html_escape(R"(ООО «Рога & Копыта» <b>"x"</b>)"),
            "ООО «Рога &amp; Копыта» &lt;b&gt;&quot;x&quot;&lt;/b&gt;");
  EXPECT_EQ(utf8_length("жж"), 2U);
}

}  // namespace
}  // namespace sk::maxapi
