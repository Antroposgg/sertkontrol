#include "sertkontrol/maxapi/update.hpp"

#include <memory>

#include <json/json.h>

namespace sk::maxapi {

namespace {

/// Поле объекта или null: `operator[]` jsoncpp бросает исключение на не-объекте (находка fuzz_update_json).
const Json::Value& field(const Json::Value& v, const char* key) {
  static const Json::Value null_value;
  return v.isObject() ? v[key] : null_value;
}

Error invalid_update(const std::string& what) {
  return Error{ErrorCode::kInvalidArgument, "Update: " + what};
}

std::int64_t int64_of(const Json::Value& v) {
  return v.isInt64() ? v.asInt64() : 0;
}
std::string string_of(const Json::Value& v) {
  return v.isString() ? v.asString() : std::string{};
}

Result<Update> parse_message_created(const Json::Value& root) {
  const auto& msg = field(root, "message");
  const auto& body = field(msg, "body");
  if (!msg.isObject() || !body.isObject() || !field(body, "mid").isString() ||
      !field(field(msg, "sender"), "user_id").isInt64()) {
    return invalid_update("message_created без message.body.mid или sender.user_id");
  }
  MessageCreated m;
  m.timestamp = int64_of(field(root, "timestamp"));
  m.mid = field(body, "mid").asString();
  m.user_id = field(field(msg, "sender"), "user_id").asInt64();
  if (field(field(msg, "recipient"), "chat_id").isInt64()) {
    m.chat_id = field(field(msg, "recipient"), "chat_id").asInt64();
  }
  m.text = string_of(field(body, "text"));
  if (field(body, "attachments").isArray()) {
    for (const auto& a : field(body, "attachments")) {
      Attachment att;
      att.type = string_of(field(a, "type"));
      att.url = string_of(field(field(a, "payload"), "url"));
      att.filename = string_of(field(a, "filename"));
      att.size = field(a, "size").isUInt64() ? field(a, "size").asUInt64() : 0;
      m.attachments.push_back(std::move(att));
    }
  }
  return Update{std::move(m)};
}

Result<Update> parse_callback(const Json::Value& root) {
  const auto& cb = field(root, "callback");
  if (!cb.isObject() || !field(cb, "callback_id").isString() ||
      !field(field(cb, "user"), "user_id").isInt64()) {
    return invalid_update("message_callback без callback.callback_id или user.user_id");
  }
  return Update{MessageCallback{.timestamp = int64_of(field(root, "timestamp")),
                                .callback_id = field(cb, "callback_id").asString(),
                                .payload = string_of(field(cb, "payload")),
                                .user_id = field(field(cb, "user"), "user_id").asInt64()}};
}

Result<Update> parse_bot_started(const Json::Value& root) {
  if (!field(field(root, "user"), "user_id").isInt64()) {
    return invalid_update("bot_started без user.user_id");
  }
  BotStarted b{.timestamp = int64_of(field(root, "timestamp")),
               .user_id = field(field(root, "user"), "user_id").asInt64(),
               .chat_id = int64_of(field(root, "chat_id"))};
  if (field(root, "payload").isString()) {
    b.payload = field(root, "payload").asString();
  }
  return Update{b};
}

}  // namespace

Result<Update> parse_update(std::string_view json) {
  Json::Value root;
  const Json::CharReaderBuilder builder;
  std::string errs;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errs) || !root.isObject() ||
      !field(root, "update_type").isString()) {
    return invalid_update("не JSON-объект с update_type");
  }
  const auto type = field(root, "update_type").asString();
  if (type == "message_created") {
    return parse_message_created(root);
  }
  if (type == "message_callback") {
    return parse_callback(root);
  }
  if (type == "bot_started") {
    return parse_bot_started(root);
  }
  return Update{OtherUpdate{.timestamp = int64_of(field(root, "timestamp")), .type = type}};
}

std::string dedup_key(const Update& update) {
  struct Visitor {
    std::string operator()(const MessageCreated& m) const { return "m:" + m.mid; }
    std::string operator()(const MessageCallback& c) const { return "c:" + c.callback_id; }
    std::string operator()(const BotStarted& b) const {
      return "s:" + std::to_string(b.user_id) + ":" + std::to_string(b.timestamp);
    }
    std::string operator()(const OtherUpdate& o) const {
      return "o:" + o.type + ":" + std::to_string(o.timestamp);
    }
  };
  return std::visit(Visitor{}, update);
}

}  // namespace sk::maxapi
