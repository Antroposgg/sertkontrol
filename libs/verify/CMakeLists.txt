# C4 — поиск и правила вердикта (R2). Нечёткий поиск — этап 3.
add_library(sk_verify STATIC src/verify.cpp src/text.cpp src/inn.cpp)
add_library(sk::verify ALIAS sk_verify)
target_include_directories(sk_verify PUBLIC include)
target_link_libraries(sk_verify PUBLIC sk::contracts sk::canon sk::snapshot)
sk_set_warnings(sk_verify)
