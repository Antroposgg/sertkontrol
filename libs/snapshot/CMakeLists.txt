# C2 — формат снапшота, writer, reader, SnapshotHolder; diff двух снапшотов (R1).
add_library(sk_snapshot STATIC src/format.cpp src/writer.cpp src/reader.cpp src/diff.cpp)
add_library(sk::snapshot ALIAS sk_snapshot)
target_include_directories(sk_snapshot PUBLIC include PRIVATE ${XXHASH_INCLUDE_DIR})
target_link_libraries(sk_snapshot PUBLIC sk::contracts sk::canon PRIVATE ${XXHASH_LIBRARY})
sk_set_warnings(sk_snapshot)
