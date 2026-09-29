#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <json/json.h>

namespace sk::maxapi::detail {

/// Разбор JSON без исключений. jsoncpp возвращает false на синтаксической ошибке, но на вложенности глубже
/// `stackLimit` (1000) бросает `Json::RuntimeError` — находка fuzz (`fuzz_update_json`, `fuzz_outbox_json`).
/// Вход приходит из сети и БД, поэтому это ошибка данных, а не программиста (CLAUDE.md, «Стиль кода»).
inline bool parse_json(std::string_view text, Json::Value& out) noexcept {
  try {
    const Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
    std::string errs;
    return reader->parse(text.data(), text.data() + text.size(), &out, &errs);
  } catch (const Json::Exception&) {
    return false;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

}  // namespace sk::maxapi::detail
