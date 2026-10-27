#include "marketsim/gateway/order_parse.hpp"

#include <cctype>

namespace marketsim::gateway {

namespace {

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<marketsim::domain::Quantity> parse_quantity(const nlohmann::json& j,
                                                                        ParseError& err) {
  if (!j.is_number_integer()) {
    err = {"invalid_quantity", "quantity must be a JSON integer"};
    return std::nullopt;
  }
  const auto v = j.get<std::int64_t>();
  if (v <= 0) {
    err = {"invalid_quantity", "quantity must be positive"};
    return std::nullopt;
  }
  return static_cast<marketsim::domain::Quantity>(v);
}

[[nodiscard]] std::optional<marketsim::domain::PriceCents> parse_price_cents(const nlohmann::json& j,
                                                                             ParseError& err) {
  if (!j.is_number_integer()) {
    err = {"invalid_price", "price field must be integer cents"};
    return std::nullopt;
  }
  const auto v = j.get<std::int64_t>();
  if (v <= 0) {
    err = {"invalid_price", "price must be positive"};
    return std::nullopt;
  }
  return static_cast<marketsim::domain::PriceCents>(v);
}

}

OrderParseResult parse_place_order_json(const nlohmann::json& body) {
  OrderParseResult out;
  ParseError err;

  if (!body.is_object()) {
    out.error = {"invalid_body", "JSON body must be an object"};
    return out;
  }

  if (!body.contains("symbol") || !body["symbol"].is_string()) {
    out.error = {"missing_symbol", "field 'symbol' (string) is required"};
    return out;
  }
  const std::string symbol = body["symbol"].get<std::string>();
  if (symbol.empty()) {
    out.error = {"invalid_symbol", "symbol must be non-empty"};
    return out;
  }

  if (!body.contains("side") || !body["side"].is_string()) {
    out.error = {"missing_side", "field 'side' (string) is required"};
    return out;
  }
  const std::string side_str = body["side"].get<std::string>();
  marketsim::domain::Side side{};
  if (iequals(side_str, "buy")) {
    side = marketsim::domain::Side::Buy;
  } else if (iequals(side_str, "sell")) {
    side = marketsim::domain::Side::Sell;
  } else {
    out.error = {"invalid_side", "side must be 'buy' or 'sell'"};
    return out;
  }

  if (!body.contains("type") || !body["type"].is_string()) {
    out.error = {"missing_type", "field 'type' (string) is required"};
    return out;
  }
  const std::string type_str = body["type"].get<std::string>();
  marketsim::domain::OrderType otype{};
  if (iequals(type_str, "limit")) {
    otype = marketsim::domain::OrderType::Limit;
  } else if (iequals(type_str, "market")) {
    otype = marketsim::domain::OrderType::Market;
  } else {
    out.error = {"invalid_type", "type must be 'limit' or 'market'"};
    return out;
  }

  if (!body.contains("quantity")) {
    out.error = {"missing_quantity", "field 'quantity' is required"};
    return out;
  }
  const auto qty = parse_quantity(body["quantity"], err);
  if (!qty.has_value()) {
    out.error = err;
    return out;
  }

  std::optional<marketsim::domain::PriceCents> limit_px;
  if (otype == marketsim::domain::OrderType::Limit) {
    const nlohmann::json* price_field = nullptr;
    if (body.contains("price_cents")) {
      price_field = &body["price_cents"];
    } else if (body.contains("price")) {
      price_field = &body["price"];
    }
    if (price_field == nullptr) {
      out.error = {"missing_price", "limit orders require 'price_cents' or 'price' (integer cents)"};
      return out;
    }
    const auto px = parse_price_cents(*price_field, err);
    if (!px.has_value()) {
      out.error = err;
      return out;
    }
    limit_px = px;
  } else {
    if (body.contains("price_cents") || body.contains("price")) {
      out.error = {"unexpected_price", "market orders must not include a limit price"};
      return out;
    }
  }

  matching::SubmitRequest req;
  req.symbol = symbol;
  req.side = side;
  req.type = otype;
  req.quantity = *qty;
  req.limit_price = limit_px;

  ParsedPlaceOrder parsed;
  parsed.request = std::move(req);
  if (body.contains("client_id")) {
    if (!body["client_id"].is_string()) {
      out.error = {"invalid_client_id", "client_id must be a string when present"};
      return out;
    }
    parsed.client_id = body["client_id"].get<std::string>();
  }

  out.ok = true;
  out.value = std::move(parsed);
  return out;
}

OrderParseResult parse_place_order_string(const std::string_view text) {
  OrderParseResult out;
  try {
    const auto j = nlohmann::json::parse(text);
    return parse_place_order_json(j);
  } catch (const nlohmann::json::parse_error& e) {
    out.error = {"invalid_json", std::string("parse error: ") + e.what()};
    return out;
  }
}

}
