# Ядро certd без main: тестируется напрямую (tests/certd). bot/ — зона R4, живёт в том же бинаре (АРХ §5).
add_library(sk_certd_core STATIC
  config.cpp
  health.cpp
  fake_domain.cpp
  media.cpp
  rate_limiter.cpp
  recognition_pool.cpp
  domain_service.cpp
  snapshot_loader.cpp
  jobs.cpp
  notify.cpp
  maintenance.cpp
  json_views.cpp
  rest_api.cpp
  pg_ports.cpp
  webhook.cpp
  bot/card.cpp
  bot/bot.cpp)
add_library(sk::certd_core ALIAS sk_certd_core)
target_include_directories(sk_certd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sk_certd_core PUBLIC
  sk::contracts sk::fakes sk::canon sk::snapshot sk::verify sk::recog sk::maxapi Drogon::Drogon)
sk_set_warnings(sk_certd_core)

add_executable(certd main.cpp)
target_link_libraries(certd PRIVATE sk::certd_core Drogon::Drogon)
sk_set_warnings(certd)
