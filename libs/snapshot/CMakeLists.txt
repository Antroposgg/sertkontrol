# C2 — формат снапшота, writer, reader, SnapshotHolder (R1). Diff — этап 2.
add_library(sk_snapshot STATIC src/format.cpp src/writer.cpp src/reader.cpp)
add_library(sk::snapshot ALIAS sk_snapshot)
target_include_directories(sk_snapshot PUBLIC include PRIVATE ${XXHASH_INCLUDE_DIR})
target_link_libraries(sk_snapshot PUBLIC sk::contracts sk::canon PRIVATE ${XXHASH_LIBRARY})
sk_set_warnings(sk_snapshot)
