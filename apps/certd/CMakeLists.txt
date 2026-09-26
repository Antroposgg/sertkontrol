sk_find_drogon()

# Ядро certd без main: тестируется напрямую (tests/certd).
add_library(sk_certd_core STATIC
  config.cpp
  health.cpp
  fake_domain.cpp)
add_library(sk::certd_core ALIAS sk_certd_core)
target_include_directories(sk_certd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sk_certd_core PUBLIC sk::contracts sk::fakes Drogon::Drogon)
sk_set_warnings(sk_certd_core)

add_executable(certd main.cpp)
target_link_libraries(certd PRIVATE sk::certd_core Drogon::Drogon)
sk_set_warnings(certd)
