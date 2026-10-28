#pragma once

#include <cstdint>
#include <string>

namespace marketsim::domain {

/// Buy vs sell from the participant's perspective.
enum class Side : std::uint8_t { Buy, Sell };

/// Supported order shapes for the matching engine.
enum class OrderType : std::uint8_t { Limit, Market };

/// Opaque order identifier assigned by the trading gateway / simulator.
using OrderId = std::uint64_t;

/// Monotonic trade identifier assigned by the matching engine.
using TradeId = std::uint64_t;

/// Monotonic tick counter for simulated exchange time (not wall clock).
using SimTime = std::uint64_t;

/// Price in integer cents to avoid floating-point rounding in core logic.
using PriceCents = std::int64_t;

/// Whole-share quantity (no fractional shares).
using Quantity = std::int64_t;

/// Tradable instrument id (e.g. "AAPL"); kept as string for clarity at boundaries.
using Symbol = std::string;

/// Participant that owns orders and receives fills.
using AccountId = std::uint32_t;

/// The human trader (REST and dashboard); the only account with a portfolio.
inline constexpr AccountId kTraderAccount = 1;

/// Simulated liquidity provider quoting around the feed price.
inline constexpr AccountId kMarketMakerAccount = 2;

/// Bots that trade against the market maker; they have no portfolio.
inline constexpr AccountId kRandomTraderAccount = 3;
inline constexpr AccountId kTrendFollowerAccount = 4;
inline constexpr AccountId kBargainHunterAccount = 5;

}
