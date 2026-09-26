/// @file init_data.hpp
/// @brief Подпись initData в тестах по официальному алгоритму MAX — напрямую через OpenSSL,
/// независимо от проверяемого кода `maxapi::validate_init_data`.
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "sertkontrol/maxapi/auth.hpp"

namespace sk::test {

/// Строка initData из пар в порядке возрастания ключа (как требует алгоритм) + `hash`.
inline std::string sign_init_data(std::string_view token,
                                  const std::vector<std::pair<std::string, std::string>>& params) {
  std::string launch;
  std::string raw;
  for (const auto& [k, v] : params) {
    launch.append(launch.empty() ? "" : "\n").append(k).append("=").append(v);
    raw.append(raw.empty() ? "" : "&").append(k).append("=").append(maxapi::percent_encode(v));
  }
  std::array<unsigned char, 32> secret{};
  std::array<unsigned char, 32> mac{};
  unsigned int len = 0;
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL принимает байты как unsigned char.
  HMAC(EVP_sha256(), "WebAppData", 10, reinterpret_cast<const unsigned char*>(token.data()), token.size(),
       secret.data(), &len);
  HMAC(EVP_sha256(), secret.data(), 32, reinterpret_cast<const unsigned char*>(launch.data()), launch.size(),
       mac.data(), &len);
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::string hex;
  for (const auto b : mac) {
    hex += kHex[b >> 4U];
    hex += kHex[b & 0x0FU];
  }
  return raw + "&hash=" + hex;
}

/// initData пользователя `user_id`, выданная в момент `auth_date` (секунды).
inline std::string init_data_for(std::string_view token, long long user_id, long long auth_date) {
  return sign_init_data(token,
                        {{"auth_date", std::to_string(auth_date)},
                         {"user", R"({"id":)" + std::to_string(user_id) + R"(,"first_name":"Тест"})"}});
}

}  // namespace sk::test
