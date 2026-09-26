/// @file clock.hpp
/// @brief «Сегодня» для расчётных правил — дата по Москве (UTC+3, без перехода на летнее время с 2014 г.).
#pragma once

#include <chrono>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Календарная дата в Москве для момента `now`.
[[nodiscard]] inline Date moscow_date(std::chrono::system_clock::time_point now) {
  return std::chrono::floor<std::chrono::days>(now + std::chrono::hours{3});
}

}  // namespace sk::certd
