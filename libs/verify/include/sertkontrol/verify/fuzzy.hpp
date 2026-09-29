/// @file fuzzy.hpp
/// @brief Нечёткий поиск номера (АРХ §7.2): взвешенный Левенштейн с таблицей путаницы OCR и кандидаты
/// из `serial_index`. Используется `verify::check`, бенчмарками и property-тестами.
#pragma once

#include <string_view>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::verify {

/// Цена замены из таблицы путаницы OCR (O↔0, B↔8, S↔5, I↔1, Z↔2).
inline constexpr double kOcrSubstitutionCost = 0.3;
/// Порог принятия: расстояние до лучшего кандидата не больше…
inline constexpr double kAcceptDistance = 2.0;
/// …и отрыв от второго кандидата не меньше.
inline constexpr double kMinGap = 0.5;

/// Взвешенное расстояние Левенштейна по кодовым точкам UTF-8: вставка, удаление и замена — 1,
/// замена пары из таблицы путаницы OCR — `kOcrSubstitutionCost`. Строки длиннее 64 кодовых точек
/// сравниваются по первым 64 (номера короче 40, АРХ §7.2).
[[nodiscard]] double weighted_distance(std::string_view a, std::string_view b);

/// Результат нечёткого поиска.
struct FuzzyMatch {
  std::vector<Suggestion> ranked{};  ///< Кандидаты по возрастанию расстояния, без точного совпадения.
  bool confident{false};  ///< Лучший ≤ `kAcceptDistance` и отрыв от второго ≥ `kMinGap`.
};

/// Кандидаты для канонического номера: записи с той же серией и годом, с серией, отличающейся одной цифрой,
/// и с серией, где буквы-двойники цифр заменены цифрами. Сложность — O(длина серии × 9) обращений к индексу.
[[nodiscard]] FuzzyMatch fuzzy_match(const snapshot::Snapshot& snap, std::string_view canonical);

}  // namespace sk::verify
