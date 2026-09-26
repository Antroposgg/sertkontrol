cmake_minimum_required(VERSION 3.28)
project(sertkontrol VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# C++20-модули не используются; сканирование ломает compile_commands.json для clang-tidy.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(SK_WERROR "Предупреждения — ошибки" ON)
option(SK_SANITIZE "Сборка с ASan/UBSan" OFF)
option(SK_COVERAGE "Инструментирование покрытия (gcov)" OFF)
option(BUILD_TESTING "Собирать тесты" ON)

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)
include(cmake/FindDrogonCompat.cmake)
sk_find_drogon()

# Порядок сверху вниз повторяет граф зависимостей АРХ §5: нижние модули не знают о верхних.
add_subdirectory(libs/contracts)
add_subdirectory(libs/canon)
add_subdirectory(libs/snapshot)
add_subdirectory(libs/verify)
add_subdirectory(libs/recog)
add_subdirectory(libs/maxapi)
add_subdirectory(apps/ingest)
add_subdirectory(apps/certd)

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
