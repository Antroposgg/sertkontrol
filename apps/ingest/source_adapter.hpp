/// @file source_adapter.hpp
/// @brief C3 — адаптер источника реестра (внутренний контракт R1). АРХ §5.
///
/// Любой источник (демо-данные, открытый набор ФСА) приводится к потоку `RawRecord`.
/// Канонизация и упаковка в снапшот — дело `ingest`, а не адаптера.
#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "sertkontrol_contracts.hpp"

namespace sk::ingest {

/// Запись источника «как есть», до канонизации. Даты — строки ISO 8601 или пусто.
struct RawRecord {
  std::string number{};
  std::string status{};
  std::string issue_date{};
  std::string expiry_date{};
  std::string status_date{};
  std::string applicant_name{};
  std::string applicant_inn{};
  std::string manufacturer_name{};
  std::string product{};
  std::string tnved{};
  std::string country{};
  std::string lab_accreditation{};
  std::uint64_t registry_id{0};
};

/// Приёмник записей: вызывается по одной записи, память не растёт с размером набора.
using RecordSink = std::function<void(RawRecord&&)>;

/// C3: источник знает своё имя и дату актуальности и выдаёт записи потоком.
/// `for_each` возвращает число выданных записей или ошибку (IO, формат).
template <class A>
concept SourceAdapter = requires(A& adapter, const RecordSink& sink) {
  { adapter.name() } -> std::convertible_to<std::string_view>;
  { adapter.source_date() } -> std::same_as<Date>;
  { adapter.for_each(sink) } -> std::same_as<Result<std::size_t>>;
};

}  // namespace sk::ingest
