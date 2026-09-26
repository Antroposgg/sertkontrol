#include "sertkontrol/maxapi/update.hpp"

#include <memory>

#include <json/json.h>

namespace sk::maxapi {

namespace {

Error invalid(const std::string& what) {
  return Error{ErrorCode::kInvalidArgument, "Update: " + what};
}

std::int64_t int64_of(const Json::Value& v) {
  return v.isIntegral() ? v.asInt64() : 0;
}
std::string string_of(const Json::Value& v) {
  return v.isString() ? v.asString() : std::string{};
}

Result<Update> parse_message_created(const Json::Value& root) {
  const auto& msg = root["message"];
  const auto& body = msg["body"];
  if (!msg.isObject() || !body.isObject() || !body["mid"].isString() ||
      !msg["sender"]["user_id"].isIntegral()) {
    return invalid("message_created без message.body.mid или sender.user_id");
  }
  MessageCreated m;
  m.timestamp = int64_of(root["timestamp"]);
  m.mid = body["mid"].asString();
  m.user_id = msg["sender"]["user_id"].asInt64();
  if (msg["recipient"]["chat_id"].isIntegral()) {
    m.chat_id = msg["recipient"]["chat_id"].asInt64();
  }
  m.text = string_of(body["text"]);
  if (body["attachments"].isArray()) {
    for (const auto& a : body["attachments"]) {
      Attachment att;
      att.type = string_of(a["type"]);
      att.url = string_of(a["payload"]["url"]);
      att.filename = string_of(a["filename"]);
      att.size = a["size"].isIntegral() ? a["size"].asUInt64() : 0;
      m.attachments.push_back(std::move(att));
    }
  }
  return Update{std::move(m)};
}

Result<Update> parse_callback(const Json::Value& root) {
  const auto& cb = root["callback"];
  if (!cb.isObject() || !cb["callback_id"].isString() || !cb["user"]["user_id"].isIntegral()) {
    return invalid("message_callback без callback.callback_id или user.user_id");
  }
  return Update{MessageCallback{.timestamp = int64_of(root["timestamp"]),
                                .callback_id = cb["callback_id"].asString(),
                                .payload = string_of(cb["payload"]),
                                .user_id = cb["user"]["user_id"].asInt64()}};
}

Result<Update> parse_bot_started(const Json::Value& root) {
  if (!root["user"]["user_id"].isIntegral()) {
    return invalid("bot_started без user.user_id");
  }
  BotStarted b{.timestamp = int64_of(root["timestamp"]),
               .user_id = root["user"]["user_id"].asInt64(),
               .chat_id = int64_of(root["chat_id"])};
  if (root["payload"].isString()) {
    b.payload = root["payload"].asString();
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
      !root["update_type"].isString()) {
    return invalid("не JSON-объект с update_type");
  }
  const auto type = root["update_type"].asString();
  if (type == "message_created") {
    return parse_message_created(root);
  }
  if (type == "message_callback") {
    return parse_callback(root);
  }
  if (type == "bot_started") {
    return parse_bot_started(root);
  }
  return Update{OtherUpdate{.timestamp = int64_of(root["timestamp"]), .type = type}};
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
