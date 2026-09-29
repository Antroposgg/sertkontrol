/// @file bot_identity.hpp
/// @brief Username бота для кнопок `open_app`: из `GET /me`, а `MAX_BOT_USERNAME` — запасное значение.
#pragma once

#include <drogon/utils/coroutine.h>

#include <string>

#include "sertkontrol/maxapi/bot_api.hpp"

namespace sk::certd {

/// Итог: username для `web_app` (пусто — кнопки `open_app` не добавляются) и предупреждение для журнала.
struct BotIdentity {
  std::string username{};
  std::string warning{};
};

/// MAX отклоняет всё сообщение целиком (HTTP 404 `Link not found`), если `web_app` кнопки `open_app` — не
/// username бота. Отображаемое имя («Хакатон МАХ 476») в `MAX_BOT_USERNAME` так и ломало ответы бота, поэтому
/// источник истины — `GET /me`; `configured` используется, только если MAX не ответил.
/// `api` — указатель: параметры корутин по значению (CLAUDE.md).
drogon::Task<BotIdentity> resolve_bot_username(maxapi::BotApi* api, std::string configured);

}  // namespace sk::certd
