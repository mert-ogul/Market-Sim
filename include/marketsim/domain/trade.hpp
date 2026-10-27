#pragma once

#include "marketsim/domain/types.hpp"

namespace marketsim::domain {

/// Executed trade between passive and aggressive orders (both ids recorded for audit).
struct Trade {
  OrderId passive_order_id{};
  OrderId aggressive_order_id{};
  Symbol symbol{};
  Side aggressive_side{Side::Buy};
  PriceCents price{0};
  Quantity quantity{0};
  SimTime time{0};
  AccountId passive_owner{};
  AccountId aggressive_owner{};
};

}
