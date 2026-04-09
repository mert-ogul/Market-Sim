#include "marketsim/portfolio/risk_manager.hpp"

#include "marketsim/portfolio/portfolio.hpp"

#include <cmath>
#include <limits>
#include <optional>

namespace marketsim::risk {

namespace {

[[nodiscard]] bool multiply_would_overflow(const domain::Quantity qty, const domain::PriceCents px) {
  if (qty <= 0 || px <= 0) {
    return false;
  }
  return qty > std::numeric_limits<domain::PriceCents>::max() / px;
}

[[nodiscard]] std::optional<domain::PriceCents>
effective_price_for(const matching::SubmitRequest& request,
                      const std::unordered_map<domain::Symbol, domain::PriceCents>& refs) {
  if (request.type == domain::OrderType::Limit) {
    return request.limit_price;
  }
  const auto it = refs.find(request.symbol);
  if (it == refs.end()) {
    return std::nullopt;
  }
  return it->second;
}

}

RiskManager::RiskManager(domain::RiskLimits limits, const portfolio::Portfolio& portfolio)
    : limits_(std::move(limits)), portfolio_(portfolio) {}

void RiskManager::set_reference(const domain::Symbol& symbol, const domain::PriceCents price_cents) {
  reference_prices_[symbol] = price_cents;
}

domain::PriceCents RiskManager::reference_for(const domain::Symbol& symbol) const {
  const auto it = reference_prices_.find(symbol);
  if (it == reference_prices_.end()) {
    return 0;
  }
  return it->second;
}

domain::PriceCents RiskManager::effective_price(const matching::SubmitRequest& request) const {
  const auto px = effective_price_for(request, reference_prices_);
  return px.value_or(0);
}

RiskCheckResult RiskManager::check(const matching::SubmitRequest& request) const {
  if (limits_.max_order_size > 0 && request.quantity > limits_.max_order_size) {
    return {false, RiskRejectReason::OrderExceedsMaxSize, "quantity above max_order_size"};
  }

  const auto px_opt = effective_price_for(request, reference_prices_);

  if (request.type == domain::OrderType::Limit && !request.limit_price.has_value()) {
    return {false, RiskRejectReason::InvalidRequest, "limit order missing limit_price"};
  }

  if (request.type == domain::OrderType::Market && request.side == domain::Side::Buy &&
      !px_opt.has_value()) {
    return {false, RiskRejectReason::UnknownSymbolReference, "no reference price for market buy"};
  }

  if (request.limit_price.has_value()) {
    const domain::PriceCents p = *request.limit_price;
    if (limits_.min_price_cents > 0 && p < limits_.min_price_cents) {
      return {false, RiskRejectReason::AbsoluteMinMaxPrice, "below min_price_cents"};
    }
    if (limits_.max_price_cents > 0 && p > limits_.max_price_cents) {
      return {false, RiskRejectReason::AbsoluteMinMaxPrice, "above max_price_cents"};
    }
  }

  if (limits_.price_collar_cents > 0 && request.limit_price.has_value()) {
    const auto ref_it = reference_prices_.find(request.symbol);
    if (ref_it != reference_prices_.end()) {
      const domain::PriceCents ref = ref_it->second;
      const domain::PriceCents p = *request.limit_price;
      const domain::PriceCents lo = ref - limits_.price_collar_cents;
      const domain::PriceCents hi = ref + limits_.price_collar_cents;
      if (p < lo || p > hi) {
        return {false, RiskRejectReason::PriceOutsideCollar, "limit outside collar around reference"};
      }
    }
  }

  if (!px_opt.has_value()) {
    return {true, RiskRejectReason::None, nullptr};
  }
  const domain::PriceCents px = *px_opt;

  if (request.side == domain::Side::Buy) {
    if (multiply_would_overflow(request.quantity, px)) {
      return {false, RiskRejectReason::InsufficientCash, "order notional overflow"};
    }
    const domain::PriceCents cost = request.quantity * px;
    if (cost > portfolio_.cash_cents()) {
      return {false, RiskRejectReason::InsufficientCash, "not enough cash_cents"};
    }
  }

  if (limits_.max_position_per_symbol > 0) {
    const domain::Quantity cur = portfolio_.position_quantity(request.symbol);
    domain::Quantity new_q = cur;
    if (request.side == domain::Side::Buy) {
      new_q += request.quantity;
    } else {
      new_q -= request.quantity;
    }
    const auto absq = static_cast<domain::Quantity>(std::llabs(static_cast<long long>(new_q)));
    if (absq > limits_.max_position_per_symbol) {
      return {false, RiskRejectReason::PositionLimitExceeded, "would exceed max_position_per_symbol"};
    }
  }

  return {true, RiskRejectReason::None, nullptr};
}

}
