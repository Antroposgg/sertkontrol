#include "sertkontrol/maxapi/message.hpp"

#include <algorithm>
#include <array>
#include <memory>

#include <json/json.h>

namespace sk::maxapi {

namespace {

constexpr std::array<std::string_view, 4> kKindNames{"reply", "verdict", "status_changed", "service"};

std::string_view kind_name(MessageKind k) {
  return kKindNames.at(static_cast<std::size_t>(k));
}

std::string_view button_type(Button::Kind k) {
  switch (k) {
    case Button::Kind::kCallback:
      return "callback";
    case Button::Kind::kLink:
      return "link";
    case Button::Kind::kOpenApp:
      return "open_app";
  }
  return "callback";
}

bool is_word_payload(std::string_view s) {
  return std::ranges::all_of(s, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  });
}

Error invalid_message(const std::string& what) {
  return Error{ErrorCode::kInvalidArgument, "OutgoingMessage: " + what};
}

std::string write(const Json::Value& v) {
  Json::StreamWriterBuilder b;
  b["indentation"] = "";
  b["emitUTF8"] = true;
  return Json::writeString(b, v);
}

}  // namespace

std::size_t utf8_length(std::string_view text) noexcept {
  return static_cast<std::size_t>(
      std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

std::string html_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(c);
    }
  }
  return out;
}

Result<Ok> validate(const OutgoingMessage& msg) {
  if (msg.max_user_id <= 0) {
    return invalid_message("нет получателя");
  }
  const auto len = utf8_length(msg.text);
  if (len == 0 || len > kMaxTextLength) {
    return invalid_message("длина текста " + std::to_string(len) + " вне 1.." + std::to_string(kMaxTextLength));
  }
  if (msg.buttons.size() > kMaxRows) {
    return invalid_message("больше " + std::to_string(kMaxRows) + " рядов кнопок");
  }
  for (const auto& row : msg.buttons) {
    if (row.empty() || row.size() > kMaxButtonsPerRow) {
      return invalid_message("в ряду 1.." + std::to_string(kMaxButtonsPerRow) + " кнопок");
    }
    const auto special =
        std::ranges::count_if(row, [](const Button& b) { return b.kind != Button::Kind::kCallback; });
    if (static_cast<std::size_t>(special) > kMaxSpecialButtonsPerRow) {
      return invalid_message("больше 3 кнопок link/open_app в ряду");
    }
    for (const auto& b : row) {
      const auto t = utf8_length(b.text);
      if (t == 0 || t > kMaxButtonText) {
        return invalid_message("текст кнопки 1.." + std::to_string(kMaxButtonText) + " символов");
      }
      switch (b.kind) {
        case Button::Kind::kCallback:
          if (b.payload.empty() || b.payload.size() > kMaxCallbackPayload) {
            return invalid_message("payload callback-кнопки 1.." + std::to_string(kMaxCallbackPayload));
          }
          break;
        case Button::Kind::kLink:
          if (!b.url.starts_with("https://")) {
            return invalid_message("ссылка кнопки должна начинаться с https://");
          }
          break;
        case Button::Kind::kOpenApp:
          if (b.payload.size() > kMaxOpenAppPayload || !is_word_payload(b.payload)) {
            return invalid_message("payload open_app: [A-Za-z0-9_-]{0,512}");
          }
          break;
      }
    }
  }
  return Ok{};
}

std::string to_outbox_json(const OutgoingMessage& msg) {
  Json::Value v{Json::objectValue};
  v["version"] = 1;
  v["max_user_id"] = static_cast<Json::Int64>(msg.max_user_id);
  v["text"] = msg.text;
  v["kind"] = std::string{kind_name(msg.kind)};
  v["is_demo"] = msg.is_demo;
  Json::Value rows{Json::arrayValue};
  for (const auto& row : msg.buttons) {
    Json::Value r{Json::arrayValue};
    for (const auto& b : row) {
      Json::Value jb{Json::objectValue};
      jb["type"] = std::string{button_type(b.kind)};
      jb["text"] = b.text;
      switch (b.kind) {
        case Button::Kind::kCallback:
          jb["payload"] = b.payload;
          break;
        case Button::Kind::kLink:
          jb["url"] = b.url;
          break;
        case Button::Kind::kOpenApp:
          jb["start_param"] = b.payload;
          break;
      }
      r.append(jb);
    }
    rows.append(r);
  }
  if (!msg.buttons.empty()) {
    v["buttons"] = rows;
  }
  return write(v);
}

Result<OutgoingMessage> from_outbox_json(std::string_view json) {
  Json::Value v;
  const Json::CharReaderBuilder builder;
  std::string errs;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &v, &errs) || !v.isObject() ||
      v["version"].asInt() != 1 || !v["max_user_id"].isIntegral() || !v["text"].isString()) {
    return invalid_message("строка outbox не соответствует C9 v1");
  }
  OutgoingMessage m;
  m.max_user_id = v["max_user_id"].asInt64();
  m.text = v["text"].asString();
  m.is_demo = v["is_demo"].asBool();
  const auto kind = v["kind"].asString();
  for (std::size_t i = 0; i < kKindNames.size(); ++i) {
    if (kKindNames.at(i) == kind) {
      m.kind = static_cast<MessageKind>(i);
    }
  }
  for (const auto& row : v["buttons"]) {
    std::vector<Button> r;
    for (const auto& jb : row) {
      Button b;
      const auto type = jb["type"].asString();
      b.text = jb["text"].asString();
      if (type == "link") {
        b.kind = Button::Kind::kLink;
        b.url = jb["url"].asString();
      } else if (type == "open_app") {
        b.kind = Button::Kind::kOpenApp;
        b.payload = jb["start_param"].asString();
      } else if (type == "callback") {
        b.payload = jb["payload"].asString();
      } else {
        return invalid_message("неизвестный тип кнопки «" + type + "»");
      }
      r.push_back(std::move(b));
    }
    m.buttons.push_back(std::move(r));
  }
  return m;
}

std::string to_new_message_body(const OutgoingMessage& msg, std::string_view bot_username) {
  Json::Value body{Json::objectValue};
  body["text"] = msg.text;
  body["format"] = "html";
  Json::Value rows{Json::arrayValue};
  for (const auto& row : msg.buttons) {
    Json::Value r{Json::arrayValue};
    for (const auto& b : row) {
      Json::Value jb{Json::objectValue};
      jb["type"] = std::string{button_type(b.kind)};
      jb["text"] = b.text;
      switch (b.kind) {
        case Button::Kind::kCallback:
          jb["payload"] = b.payload;
          break;
        case Button::Kind::kLink:
          jb["url"] = b.url;
          break;
        case Button::Kind::kOpenApp:
          if (bot_username.empty()) {
            continue;  // без web_app MAX кнопку не примет
          }
          jb["web_app"] = std::string{bot_username};
          if (!b.payload.empty()) {
            jb["payload"] = b.payload;
          }
          break;
      }
      r.append(jb);
    }
    if (!r.empty()) {
      rows.append(r);
    }
  }
  if (!rows.empty()) {
    Json::Value kb{Json::objectValue};
    kb["type"] = "inline_keyboard";
    kb["payload"]["buttons"] = rows;
    body["attachments"].append(kb);
  }
  return write(body);
}

}  // namespace sk::maxapi
