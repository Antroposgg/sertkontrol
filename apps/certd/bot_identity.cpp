#include "bot_identity.hpp"

#include <optional>
#include <string>
#include <utility>

namespace sk::certd {

drogon::Task<BotIdentity> resolve_bot_username(maxapi::BotApi* api, std::string configured) {
  const auto me = co_await api->get_me();
  if (!me) {
    if (configured.empty()) {
      co_return BotIdentity{.warning = "GET /me не удался (" + me.error().detail +
                                       "), MAX_BOT_USERNAME не задан: кнопки open_app выключены"};
    }
    co_return BotIdentity{.username = configured,
                          .warning = "GET /me не удался (" + me.error().detail +
                                     "): для open_app используется MAX_BOT_USERNAME=" + configured};
  }
  const std::optional<std::string>& found = me.value().username;
  if (!found.has_value()) {
    co_return BotIdentity{.warning = "у бота в MAX не задан username: кнопки open_app выключены"};
  }
  std::string username = *found;
  if (!configured.empty() && configured != username) {
    co_return BotIdentity{.username = username,
                          .warning = "MAX_BOT_USERNAME=" + configured + " не совпадает с username бота " +
                                     username + " из GET /me — используется " + username};
  }
  co_return BotIdentity{.username = std::move(username)};
}

}  // namespace sk::certd
