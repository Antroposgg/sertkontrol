/// @file pg.hpp
/// @brief Доступ тестов к временному PostgreSQL (scripts/ci/with-pg.sh).
#pragma once

#include <cstdlib>
#include <optional>
#include <random>
#include <string>

namespace sk::test {

/// Строка подключения из `SK_TEST_PG` или `nullopt`, если БД не поднята.
inline std::optional<std::string> test_pg() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): читается в однопоточной настройке теста.
  const char* v = std::getenv("SK_TEST_PG");
  if (v == nullptr || *v == '\0') {
    return std::nullopt;
  }
  return std::string{v};
}

/// Случайное положительное число для уникальных ключей в общей БД (тесты гоняются дважды).
inline long long unique_id() {
  static std::mt19937_64 rng{std::random_device{}()};
  return static_cast<long long>(rng() % 1'000'000'000'000ULL) + 1'000'000;
}

}  // namespace sk::test
