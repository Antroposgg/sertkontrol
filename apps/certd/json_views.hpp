/// @file json_views.hpp
/// @brief JSON-представления C7 (`openapi.yaml`) для доменных типов и ошибки RFC 9457.
#pragma once

#include <json/value.h>

#include "domain.hpp"

namespace sk::certd {

/// HTTP-статус для кода ошибки домена (RFC 9457, АРХ §8).
[[nodiscard]] int http_status(ErrorCode code) noexcept;

/// `application/problem+json`: `type`, `title`, `status`, `detail`, `code`.
[[nodiscard]] Json::Value problem_json(const Error& error);

[[nodiscard]] std::string iso_date(Date d);
[[nodiscard]] Json::Value to_json(const verify::Verdict& v);
[[nodiscard]] Json::Value to_json(const CheckedVerdict& v);
[[nodiscard]] Json::Value to_json(const PortfolioItem& item);
[[nodiscard]] Json::Value to_json(const Page<PortfolioItem>& page);
[[nodiscard]] Json::Value to_json(const AddResult& r);
[[nodiscard]] Json::Value to_json(const Me& me);
[[nodiscard]] Json::Value to_json(const DataStatus& s);
[[nodiscard]] Json::Value to_json(const DocumentHistory& h);
[[nodiscard]] Json::Value to_json(const DemoUpdate& u);

}  // namespace sk::certd
