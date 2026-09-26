# C1 — канонизация и грамматика номера (R2).
find_path(XXHASH_INCLUDE_DIR xxhash.h REQUIRED)
find_library(XXHASH_LIBRARY xxhash REQUIRED)

add_library(sk_canon STATIC src/canon.cpp src/utf8.cpp)
add_library(sk::canon ALIAS sk_canon)
target_include_directories(sk_canon PRIVATE src ${XXHASH_INCLUDE_DIR})
target_link_libraries(sk_canon PUBLIC sk::contracts PRIVATE ${XXHASH_LIBRARY})
sk_set_warnings(sk_canon)
