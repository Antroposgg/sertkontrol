find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# sk_add_test(<имя> <исходники...> LIBS <цели...>)
function(sk_add_test name)
  cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
  add_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
  target_link_libraries(${name} PRIVATE ${ARG_LIBS} GTest::gtest_main GTest::gmock)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  sk_set_warnings(${name})
  gtest_discover_tests(${name} DISCOVERY_TIMEOUT 30)
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
  LIBS sk::snapshot sk::fakes)
target_include_directories(snapshot_test PRIVATE ${XXHASH_INCLUDE_DIR})
target_link_libraries(snapshot_test PRIVATE ${XXHASH_LIBRARY})

sk_add_test(verify_test
  verify/rules_test.cpp
  verify/text_test.cpp
  LIBS sk::verify)
target_compile_definitions(verify_test PRIVATE SK_RULES_MD="${PROJECT_SOURCE_DIR}/docs/rules.md")

sk_add_test(certd_test
  certd/config_test.cpp
  certd/health_test.cpp
  certd/fake_domain_test.cpp
  LIBS sk::certd_core)

sk_add_test(ingest_test
  ingest/cli_test.cpp
  ingest/fake_source_test.cpp
  ingest/demo_build_test.cpp
  LIBS sk::ingest_core)
target_compile_definitions(ingest_test PRIVATE
  SK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  SK_DEMO_DIR="${PROJECT_SOURCE_DIR}/data/demo")
