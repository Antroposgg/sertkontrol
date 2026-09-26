# Контракты C1, C2, C4, C5 — только заголовок (ADR-0008).
add_library(sk_contracts INTERFACE)
add_library(sk::contracts ALIAS sk_contracts)
target_include_directories(sk_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(sk_contracts INTERFACE cxx_std_20)

# Fake-реализации контрактов для потребителей до готовности поставщиков (АРХ §5).
add_library(sk_fakes STATIC src/fakes.cpp)
add_library(sk::fakes ALIAS sk_fakes)
target_link_libraries(sk_fakes PUBLIC sk::contracts)
sk_set_warnings(sk_fakes)
