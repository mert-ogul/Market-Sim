#pragma once

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/trade.hpp"
#include "marketsim/domain/types.hpp"

#include <optional>
#include <vector>

namespace marketsim::matching {

enum class SubmitRejectReason {
  None,
  InvalidQuantity,
  InvalidLimitPrice,
  /// Market order with nothing to trade against (IOC).
  MarketNoLiquidity,
};

/// One trade emitted by the engine (includes engine-assigned trade id).
struct TradeFill {
  domain::TradeId trade_id{0};
  domain::Trade trade{};
};

struct SubmitRequest {
  domain::Symbol symbol{};
  domain::Side side{domain::Side::Buy};
  domain::OrderType type{domain::OrderType::Limit};
  /// Required for limit orders; ignored for market orders.
  std::optional<domain::PriceCents> limit_price{};
  domain::Quantity quantity{0};
  domain::AccountId owner{domain::kTraderAccount};
};

struct SubmitResult {
  bool accepted{false};
  SubmitRejectReason reject{SubmitRejectReason::None};
  domain::OrderId order_id{0};
  /// Resting size after submit (0 for fully filled or market IOC).
  domain::Quantity resting_quantity{0};
  std::vector<TradeFill> trades;
  /// Resting orders of the same owner that this order would have crossed; they are cancelled
  /// instead of trading (self-trade prevention, cancel-resting).
  std::vector<domain::OrderId> self_trade_cancelled_ids;
  std::optional<domain::Quote> quote_after;
};

struct CancelResult {
  bool cancelled{false};
  std::optional<domain::Quote> quote_after;
};

}
