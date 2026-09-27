# Клиент Bot API MAX, initData, разбор событий, C9 (R4).
find_package(OpenSSL REQUIRED)

add_library(sk_maxapi STATIC src/auth.cpp src/update.cpp src/message.cpp src/http_bot_api.cpp
  src/rate_limit.cpp)
add_library(sk::maxapi ALIAS sk_maxapi)
target_include_directories(sk_maxapi PUBLIC include)
target_link_libraries(sk_maxapi PUBLIC sk::contracts Drogon::Drogon PRIVATE OpenSSL::Crypto)
sk_set_warnings(sk_maxapi)
