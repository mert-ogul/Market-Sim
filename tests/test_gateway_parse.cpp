#include <gtest/gtest.h>

#include "marketsim/gateway/order_parse.hpp"

#include <nlohmann/json.hpp>

namespace {

using marketsim::gateway::parse_place_order_json;
using marketsim::gateway::parse_place_order_string;

TEST(GatewayParse, ValidLimitOrder) {
  const nlohmann::json j = nlohmann::json::parse(R"({
    "symbol": "AAPL",
    "side": "buy",
    "type": "limit",
    "quantity": 5,
    "price_cents": 100
  })");
  const auto r = parse_place_order_json(j);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.value.request.symbol, "AAPL");
  EXPECT_EQ(r.value.request.quantity, 5);
  ASSERT_TRUE(r.value.request.limit_price.has_value());
  EXPECT_EQ(*r.value.request.limit_price, 100);
}

TEST(GatewayParse, ClientIdOptional) {
  const auto r = parse_place_order_string(
      R"({"symbol":"X","side":"SELL","type":"LIMIT","quantity":1,"price":200,"client_id":"c1"})");
  ASSERT_TRUE(r.ok);
  ASSERT_TRUE(r.value.client_id.has_value());
  EXPECT_EQ(*r.value.client_id, "c1");
}

TEST(GatewayParse, InvalidJson) {
  const auto r = parse_place_order_string("{");
  ASSERT_FALSE(r.ok);
  EXPECT_EQ(r.error.code, "invalid_json");
}

TEST(GatewayParse, MissingSymbol) {
  const auto r = parse_place_order_json(nlohmann::json::object());
  ASSERT_FALSE(r.ok);
  EXPECT_EQ(r.error.code, "missing_symbol");
}

TEST(GatewayParse, LimitMissingPrice) {
  const nlohmann::json j = {{"symbol", "A"}, {"side", "buy"}, {"type", "limit"}, {"quantity", 1}};
  const auto r = parse_place_order_json(j);
  ASSERT_FALSE(r.ok);
  EXPECT_EQ(r.error.code, "missing_price");
}

TEST(GatewayParse, MarketWithPriceRejected) {
  const nlohmann::json j = {
      {"symbol", "A"}, {"side", "buy"}, {"type", "market"}, {"quantity", 1}, {"price_cents", 1}};
  const auto r = parse_place_order_json(j);
  ASSERT_FALSE(r.ok);
  EXPECT_EQ(r.error.code, "unexpected_price");
}

}
