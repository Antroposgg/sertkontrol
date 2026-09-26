/// @file update.hpp
/// @brief Разбор событий webhook MAX (`Update`). Поля — по `schema.yaml` официального клиента
/// `max-messenger/max-bot-api-client-go` (сверено 26.09.2026, docs/plan.md §5.1).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::maxapi {

/// Вложение входящего сообщения. Для `file` MAX передаёт `payload.url`, `filename` и `size`.
struct Attachment {
  std::string type{};      ///< `file`, `image`, …
  std::string url{};       ///< `payload.url`
  std::string filename{};  ///< Только для `file`.
  std::uint64_t size{0};   ///< Байт; только для `file`.
};

/// `message_created`: пользователь написал боту.
struct MessageCreated {
  std::int64_t timestamp{0};
  std::string mid{};                      ///< `message.body.mid` — ключ дедупликации.
  std::int64_t user_id{0};                ///< `message.sender.user_id`.
  std::optional<std::int64_t> chat_id{};  ///< `message.recipient.chat_id`.
  std::string text{};
  std::vector<Attachment> attachments{};
};

/// `message_callback`: пользователь нажал кнопку.
struct MessageCallback {
  std::int64_t timestamp{0};
  std::string callback_id{};
  std::string payload{};
  std::int64_t user_id{0};
};

/// `bot_started`: пользователь впервые начал диалог или возобновил его.
struct BotStarted {
  std::int64_t timestamp{0};
  std::int64_t user_id{0};
  std::int64_t chat_id{0};
  std::optional<std::string> payload{};
};

/// Любое другое событие — принимается и игнорируется.
struct OtherUpdate {
  std::int64_t timestamp{0};
  std::string type{};
};

using Update = std::variant<MessageCreated, MessageCallback, BotStarted, OtherUpdate>;

/// Разбирает тело webhook. `kInvalidArgument` — не JSON или нет обязательных полей.
[[nodiscard]] Result<Update> parse_update(std::string_view json);

/// Ключ дедупликации (АРХ §4, поток A): `m:<mid>`, `c:<callback_id>`, `s:<user_id>:<timestamp>`,
/// `o:<type>:<timestamp>`.
[[nodiscard]] std::string dedup_key(const Update& update);

}  // namespace sk::maxapi
