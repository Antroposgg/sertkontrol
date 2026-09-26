/// @file message.hpp
/// @brief C9 `OutgoingMessage` — строка outbox (docs/contracts/outgoing_message.schema.json) и её перевод
/// в `NewMessageBody` Bot API MAX. Лимиты — dev.max.ru, сверено 26.09.2026 (docs/plan.md §5.1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::maxapi {

inline constexpr std::size_t kMaxTextLength = 4000;  ///< Символов в `text`.
inline constexpr std::size_t kMaxRows = 30;          ///< Рядов клавиатуры.
inline constexpr std::size_t kMaxButtonsPerRow = 7;
inline constexpr std::size_t kMaxSpecialButtonsPerRow = 3;  ///< `link`, `open_app`.
inline constexpr std::size_t kMaxButtonText = 128;
inline constexpr std::size_t kMaxCallbackPayload = 1024;
inline constexpr std::size_t kMaxOpenAppPayload = 512;

/// Кнопка inline-клавиатуры.
struct Button {
  enum class Kind : std::uint8_t { kCallback, kLink, kOpenApp };
  Kind kind{Kind::kCallback};
  std::string text{};
  std::string payload{};  ///< callback: `<действие>:<id>`; open_app: параметр запуска `[\w-]{0,512}`.
  std::string url{};  ///< link: `https://…`.
};

/// Назначение сообщения (для метрик и приоритета outbox).
enum class MessageKind : std::uint8_t { kReply, kVerdict, kStatusChanged, kService };

/// Сообщение пользователю — полезная нагрузка outbox (C9).
struct OutgoingMessage {
  std::int64_t max_user_id{0};
  std::string text{};  ///< HTML: `<b>` и экранированный текст (`html_escape`).
  MessageKind kind{MessageKind::kReply};
  bool is_demo{false};
  std::vector<std::vector<Button>> buttons{};
};

/// Проверяет лимиты MAX. `kInvalidArgument` с описанием первого нарушения.
[[nodiscard]] Result<Ok> validate(const OutgoingMessage& msg);

/// Экранирование для `format: html`: `&`, `<`, `>`, `"`.
[[nodiscard]] std::string html_escape(std::string_view text);

/// Длина UTF-8 строки в символах (кодовых точках).
[[nodiscard]] std::size_t utf8_length(std::string_view text) noexcept;

/// Строка outbox (C9, версия схемы 1).
[[nodiscard]] std::string to_outbox_json(const OutgoingMessage& msg);
/// Разбор строки outbox. `kInvalidArgument`, если JSON не соответствует C9.
[[nodiscard]] Result<OutgoingMessage> from_outbox_json(std::string_view json);

/// Тело `POST /messages` (NewMessageBody). Кнопки `open_app` требуют публичное имя бота (`web_app`);
/// при пустом `bot_username` они опускаются.
[[nodiscard]] std::string to_new_message_body(const OutgoingMessage& msg, std::string_view bot_username);

}  // namespace sk::maxapi
