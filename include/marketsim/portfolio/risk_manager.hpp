#pragma once

#include "marketsim/domain/risk_config.hpp"
#include "marketsim/domain/types.hpp"
#include "marketsim/matching/types.hpp"

#include <unordered_map>

namespace marketsim::portfolio {
class Portfolio;
}

namespace marketsim::risk {

/// Machine-readable reason when a pre-trade check fails.
enum class RiskRejectReason {
  None,
  InvalidRequest,
  UnknownSymbolReference,  // market buy needs reference for cash
  OrderExceedsMaxSize,
  PositionLimitExceeded,
  InsufficientCash,
  PriceOutsideCollar,
  AbsoluteMinMaxPrice,
};

/// Result of `RiskManager::check`; no hidden mutations — portfolio is read-only.
struct RiskCheckResult {
  bool allowed{true};
  RiskRejectReason reason{RiskRejectReason::None};
  /// Optional detail for logs; stable codes live in `reason`.
  const char* detail{nullptr};
};

/// Pre-trade validation against limits, cash, and reference-linked collar.
class RiskManager {
public:
  RiskManager(domain::RiskLimits limits, const portfolio::Portfolio& portfolio);

  /// Update last trade / quote reference used for collars and market-order economics.
  void set_reference(const domain::Symbol& symbol, domain::PriceCents price_cents);

  [[nodiscard]] RiskCheckResult check(const matching::SubmitRequest& request) const;

  [[nodiscard]] const domain::RiskLimits& limits() const noexcept { return limits_; }

private:
  [[nodiscard]] domain::PriceCents reference_for(const domain::Symbol& symbol) const;
  [[nodiscard]] domain::PriceCents effective_price(const matching::SubmitRequest& request) const;

  domain::RiskLimits limits_;
  const portfolio::Portfolio& portfolio_;
  std::unordered_map<domain::Symbol, domain::PriceCents> reference_prices_;
};

}
