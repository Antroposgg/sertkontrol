/// @file webhook.hpp
/// @brief `POST /max/webhook` (АРХ §4, поток A, шаг 1): секрет → разбор → дедупликация → 200 → обработка в
/// фоне.
#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <string>

#include "bot/bot.hpp"
#include "ports.hpp"

namespace sk::certd {

class Webhook {
 public:
  Webhook(bot::Bot& bot, InboundLog& inbound, std::string secret)
      : bot_(bot), inbound_(inbound), secret_(std::move(secret)) {}

  /// 401 — неверный секрет; 400 — не событие MAX; 200 — принято (новое или повтор).
  /// Ответ не ждёт обработки: MAX требует 200 не позже 30 с, цель — ≤ 100 мс (АРХ §2).
  drogon::Task<drogon::HttpResponsePtr> handle(drogon::HttpRequestPtr req);

  /// Обработка события (вызывается в фоне; в тестах — напрямую).
  drogon::Task<void> process(maxapi::Update update, std::string dedup_key);

 private:
  bot::Bot& bot_;
  InboundLog& inbound_;
  std::string secret_;
};

}  // namespace sk::certd
