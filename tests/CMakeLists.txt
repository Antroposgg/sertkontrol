find_package(GTest CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
include(GoogleTest)

# sk_add_test(<имя> <исходники...> LIBS <цели...> [LABELS <метки...>])
function(sk_add_test name)
  cmake_parse_arguments(ARG "" "" "LIBS;LABELS" ${ARGN})
  add_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
  target_link_libraries(${name} PRIVATE ${ARG_LIBS} GTest::gtest_main GTest::gmock)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  sk_set_warnings(${name})
  # Тесты не входят в образ; unity склеил бы одноимённые константы разных тестовых файлов.
  set_target_properties(${name} PROPERTIES UNITY_BUILD OFF)
  if(ARG_LABELS)
    gtest_discover_tests(${name} DISCOVERY_TIMEOUT 30 PROPERTIES LABELS "${ARG_LABELS}")
  else()
    gtest_discover_tests(${name} DISCOVERY_TIMEOUT 30)
  endif()
endfunction()

sk_add_test(contracts_test
  contracts/result_test.cpp
  contracts/enums_test.cpp
  contracts/fakes_test.cpp
  LIBS sk::fakes)

sk_add_test(canon_test
  canon/golden_test.cpp
  canon/canon_test.cpp
  LIBS sk::canon)
target_compile_definitions(canon_test PRIVATE SK_CANON_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/canon_golden.tsv")

sk_add_test(snapshot_test
  snapshot/snapshot_test.cpp
  snapshot/diff_test.cpp
  LIBS sk::snapshot sk::fakes)
target_include_directories(snapshot_test PRIVATE ${XXHASH_INCLUDE_DIR})
target_link_libraries(snapshot_test PRIVATE ${XXHASH_LIBRARY})

sk_add_test(verify_test
  verify/rules_test.cpp
  verify/text_test.cpp
  verify/inn_test.cpp
  verify/fuzzy_test.cpp
  LIBS sk::verify)
target_compile_definitions(verify_test PRIVATE SK_RULES_MD="${PROJECT_SOURCE_DIR}/docs/rules.md")

sk_add_test(recog_test
  recog/recog_test.cpp
  LIBS sk::recog sk::canon)
target_compile_definitions(recog_test PRIVATE SK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

sk_add_test(maxapi_test
  maxapi/auth_test.cpp
  maxapi/update_message_test.cpp
  maxapi/rate_limit_test.cpp
  LIBS sk::maxapi OpenSSL::Crypto)
target_compile_definitions(maxapi_test PRIVATE SK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Настоящий TLS: openssl s_server с самоподписанным сертификатом (ADR-0015).
sk_add_test(maxapi_tls_test
  maxapi/tls_test.cpp
  LIBS sk::maxapi)

# Отдельный бинарь: поднимает drogon::app() (синглтон процесса) как локальный «MAX».
sk_add_test(maxapi_http_test
  maxapi/http_bot_api_test.cpp
  LIBS sk::maxapi)

sk_add_test(certd_test
  certd/config_test.cpp
  certd/health_test.cpp
  certd/fake_domain_test.cpp
  certd/rest_api_test.cpp
  certd/bot_test.cpp
  certd/components_test.cpp
  LIBS sk::certd_core OpenSSL::Crypto)

find_package(yaml-cpp REQUIRED)
sk_add_test(openapi_test
  certd/openapi_test.cpp
  LIBS sk::certd_core yaml-cpp)
target_compile_definitions(openapi_test PRIVATE SK_OPENAPI_YAML="${PROJECT_SOURCE_DIR}/openapi.yaml")

sk_add_test(certd_pg_test
  certd/pg_test.cpp
  LIBS sk::certd_core)
target_compile_definitions(certd_pg_test PRIVATE SK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Сценарий жюри целиком: демо-пара собирается из data/demo тем же кодом, что и в ingest.
sk_add_test(certd_jury_test
  certd/jury_scenario_test.cpp
  LIBS sk::certd_core sk::ingest_core)
target_compile_definitions(certd_jury_test PRIVATE
  SK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  SK_DEMO_DIR="${PROJECT_SOURCE_DIR}/data/demo")

sk_add_test(ingest_test
  ingest/cli_test.cpp
  ingest/fake_source_test.cpp
  ingest/demo_build_test.cpp
  LIBS sk::ingest_core)
target_compile_definitions(ingest_test PRIVATE
  SK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  SK_DEMO_DIR="${PROJECT_SOURCE_DIR}/data/demo")
# Тест демо-пары слушает NOTIFY snapshot_ready напрямую через libpq.
find_package(PostgreSQL REQUIRED)
target_link_libraries(ingest_test PRIVATE PostgreSQL::PostgreSQL)

# Конкурентность (АРХ §10): гоняется во всех пресетах; в пресете clang-tsan — только тесты с меткой tsan.
sk_add_test(concurrency_test
  concurrency/concurrency_test.cpp
  LIBS sk::certd_core
  LABELS tsan)
