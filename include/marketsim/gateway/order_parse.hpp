#pragma once

#include "marketsim/matching/types.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace marketsim::gateway {

struct ParsedPlaceOrder {
  matching::SubmitRequest request;
  std::optional<std::string> client_id;
};

struct ParseError {
  std::string code;
  std::string message;
};

struct OrderParseResult {
  bool ok{false};
  ParseError error{};
  ParsedPlaceOrder value{};
};

/// Strongly typed parse + validation for POST /api/v1/orders bodies.
[[nodiscard]] OrderParseResult parse_place_order_json(const nlohmann::json& body);

/// Parses JSON text; invalid JSON returns ok=false with code invalid_json.
[[nodiscard]] OrderParseResult parse_place_order_string(std::string_view text);

}
