#include "sertkontrol/maxapi/auth.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <vector>

#include <json/json.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace sk::maxapi {

namespace {

using Digest = std::array<unsigned char, 32>;

Digest hmac_sha256(std::string_view key, std::string_view data) {
  Digest out{};
  unsigned int len = 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL принимает байты как unsigned char.
  const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), bytes, data.size(), out.data(), &len);
  return out;
}

std::string to_hex(const Digest& d) {
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::string out;
  out.reserve(d.size() * 2);
  for (const auto b : d) {
    out.push_back(kHex[b >> 4U]);
    out.push_back(kHex[b & 0x0FU]);
  }
  return out;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

Error unauthorized(std::string detail) {
  return Error{ErrorCode::kUnauthorized, std::move(detail)};
}

}  // namespace

std::optional<std::string> percent_decode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%') {
      out.push_back(s[i]);
      continue;
    }
    if (i + 2 >= s.size()) {
      return std::nullopt;
    }
    const int hi = hex_value(s[i + 1]);
    const int lo = hex_value(s[i + 2]);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    out.push_back(static_cast<char>((hi << 4) | lo));
    i += 2;
  }
  return out;
}

std::string percent_encode(std::string_view s) {
  static constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string out;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) {
      out.push_back(ch);
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4U]);
      out.push_back(kHex[c & 0x0FU]);
    }
  }
  return out;
}

bool secret_matches(std::string_view received, std::string_view expected) noexcept {
  if (expected.empty() || received.size() != expected.size()) {
    return false;
  }
  return CRYPTO_memcmp(received.data(), expected.data(), expected.size()) == 0;
}

Result<InitData> validate_init_data(std::string_view raw, std::string_view bot_token,
                                    std::chrono::system_clock::time_point now, std::chrono::seconds max_age) {
  if (raw.empty()) {
    return unauthorized("нет initData");
  }
  if (bot_token.empty()) {
    return unauthorized("токен бота не настроен");
  }
  // Если строку целиком закодировали ещё раз (весь WebAppData из URL-фрагмента), раскодируем один раз —
  // так же поступает официальный Go-клиент MAX.
  std::string decoded_whole;
  if (raw.find('&') == std::string_view::npos && raw.find("%26") != std::string_view::npos) {
    auto d = percent_decode(raw);
    if (!d) {
      return unauthorized("initData: некорректное кодирование");
    }
    decoded_whole = std::move(*d);
    raw = decoded_whole;
  }

  std::map<std::string, std::string> params;
  std::optional<std::string> received_hash;
  std::size_t start = 0;
  while (start <= raw.size()) {
    const auto amp = raw.find('&', start);
    const auto pair = raw.substr(start, amp == std::string_view::npos ? std::string_view::npos : amp - start);
    const auto eq = pair.find('=');
    if (eq == std::string_view::npos || eq == 0) {
      return unauthorized("initData: некорректная пара ключ=значение");
    }
    const std::string key{pair.substr(0, eq)};
    auto value = percent_decode(pair.substr(eq + 1));
    if (!value) {
      return unauthorized("initData: некорректное кодирование");
    }
    if (key == "hash") {
      if (received_hash) {
        return unauthorized("initData: hash встречается больше одного раза");
      }
      received_hash = std::move(*value);
    } else if (!params.emplace(key, std::move(*value)).second) {
      return unauthorized("initData: повтор параметра " + key);
    }
    if (amp == std::string_view::npos) {
      break;
    }
    start = amp + 1;
  }
  if (!received_hash) {
    return unauthorized("initData: нет hash");
  }

  std::string launch_params;
  for (const auto& [k, v] : params) {  // std::map — уже по возрастанию ключа
    if (!launch_params.empty()) {
      launch_params.push_back('\n');
    }
    launch_params.append(k).append("=").append(v);
  }
  const auto secret = hmac_sha256("WebAppData", bot_token);
  const auto expected = to_hex(hmac_sha256(
      {reinterpret_cast<const char*>(secret.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
       secret.size()},
      launch_params));
  if (!secret_matches(*received_hash, expected)) {
    return unauthorized("initData: подпись не совпадает");
  }

  InitData out;
  const auto auth = params.find("auth_date");
  if (auth == params.end()) {
    return unauthorized("initData: нет auth_date");
  }
  try {
    out.auth_date = std::stoll(auth->second);
  } catch (const std::exception&) {
    return unauthorized("initData: некорректный auth_date");
  }
  const auto issued = std::chrono::system_clock::time_point{std::chrono::seconds{out.auth_date}};
  if (now - issued > max_age) {
    return Error{ErrorCode::kInitDataExpired, "initData старше 24 часов: переоткройте приложение из чата"};
  }
  const auto user = params.find("user");
  if (user == params.end()) {
    return unauthorized("initData: нет user");
  }
  Json::Value json;
  const Json::CharReaderBuilder builder;
  std::string errs;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  const auto& text = user->second;
  if (!reader->parse(text.data(), text.data() + text.size(), &json, &errs) || !json.isObject() ||
      !json["id"].isInt64()) {
    return unauthorized("initData: некорректный user");
  }
  out.user_id = json["id"].asInt64();
  if (json["first_name"].isString()) {
    out.first_name = json["first_name"].asString();
  }
  if (const auto sp = params.find("start_param"); sp != params.end()) {
    out.start_param = sp->second;
  }
  return out;
}

}  // namespace sk::maxapi
