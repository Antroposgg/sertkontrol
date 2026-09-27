/// @file schedule.hpp
/// @brief Расписание `ingest --daemon`: ежедневно в 04:00 МСК (АРХ §3, §4 поток B, шаг 1).
#pragma once

#include <chrono>

namespace sk::ingest {

/// Время запуска по Москве и её смещение от UTC (UTC+3 без перехода на летнее время с 2014 г.).
inline constexpr std::chrono::minutes kDailyRunAt{4 * 60};
inline constexpr std::chrono::minutes kMoscowOffset{3 * 60};

/// Ближайший момент строго после `now`, когда местное время (UTC + `utc_offset`) равно `at_local`
/// (`0 ≤ at_local < 24 ч`). Фиксированное смещение: зона без перехода на летнее время.
[[nodiscard]] constexpr std::chrono::system_clock::time_point next_daily_run(
    std::chrono::system_clock::time_point now, std::chrono::minutes at_local,
    std::chrono::minutes utc_offset) {
  const auto local = now + utc_offset;
  auto candidate = std::chrono::floor<std::chrono::days>(local) + at_local;
  if (candidate <= local) {
    candidate += std::chrono::days{1};
  }
  return candidate - utc_offset;
}

}  // namespace sk::ingest
