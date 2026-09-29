# Google Benchmark против целей АРХ §2. Сборка: пресет `bench`; запуск и проверка порогов: scripts/ci/bench.sh.
find_package(benchmark REQUIRED)
add_executable(search_bench search_bench.cpp)
target_link_libraries(search_bench PRIVATE sk::verify sk::snapshot benchmark::benchmark)
sk_set_warnings(search_bench)
