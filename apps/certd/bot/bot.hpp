/// @file bot.hpp
/// @brief Диалоги бота MAX (R4): события webhook → `DomainService` (C6) → сообщения в outbox (C9).
///
/// Бот не трогает домен напрямую (АРХ §5): только через `DomainService`, поэтому бот и мини-приложение
/// показывают одинаковый вердикт.
#pragma once

#include <drogon/utils/coroutine.h>

#include <cstddef>
#include <string>

#include "../domain.hpp"
#include "../ports.hpp"
#include "card.hpp"
#include "sertkontrol/maxapi/bot_api.hpp"
#include "sertkontrol/maxapi/update.hpp"

namespace sk::certd::bot {

/// Настройки бота.
struct BotConfig {
  std::size_t max_file_bytes{std::size_t{20} * 1024 * 1024};  ///< Лимит вложения до скачивания (АРХ §10).
  CardOptions card{};
};

class Bot {
 public:
  Bot(DomainService& domain, Outbox& outbox, maxapi::BotApi& api, BotConfig config)
      : domain_(domain), outbox_(outbox), api_(api), config_(config) {}

  /// Обрабатывает одно событие. Ошибка — текст для `inbound_update.error`; пустая строка — успех.
  drogon::Task<std::string> handle(maxapi::Update update);

 private:
  drogon::Task<std::string> on_started(maxapi::BotStarted e);
  drogon::Task<std::string> on_message(maxapi::MessageCreated e);
  drogon::Task<std::string> on_callback(maxapi::MessageCallback e);
  drogon::Task<std::string> send(maxapi::OutgoingMessage msg, int priority);
  drogon::Task<std::string> send_result(std::int64_t user, Result<CheckResult> result);

  DomainService& domain_;
  Outbox& outbox_;
  maxapi::BotApi& api_;
  BotConfig config_;
};

}  // namespace sk::certd::bot
