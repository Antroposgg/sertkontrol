find_package(PostgreSQL REQUIRED)

add_library(sk_ingest_core STATIC cli.cpp normalize.cpp demo_source.cpp registry_db.cpp demo_pair.cpp)
add_library(sk::ingest_core ALIAS sk_ingest_core)
target_include_directories(sk_ingest_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sk_ingest_core PUBLIC sk::contracts sk::canon sk::snapshot PRIVATE PostgreSQL::PostgreSQL)
sk_set_warnings(sk_ingest_core)

add_executable(ingest main.cpp)
target_link_libraries(ingest PRIVATE sk::ingest_core)
sk_set_warnings(ingest)
