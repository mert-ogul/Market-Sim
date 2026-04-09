#pragma once

#include "marketsim/domain/types.hpp"

#include <optional>

namespace marketsim::domain {

/// Resting or working order in the simulator. No validation logic here—callers
/// enforce invariants at system boundaries.
struct Order {
  OrderId id{};
  Symbol symbol{};
  Side side{Side::Buy};
  OrderType type{OrderType::Limit};
  /// Limit price when @c type is Limit; unset for market orders (filled at crossing).
  std::optional<PriceCents> limit_price{};
  /// Original order size in shares.
  Quantity quantity{0};
  /// Remaining size after partial fills; equals @c quantity until fills occur.
  Quantity remaining{0};
  SimTime created_time{0};
};

}
