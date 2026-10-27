#include "marketsim/gateway/demo_seed.hpp"

#include "marketsim/gateway/simulator_service.hpp"
#include "marketsim/matching/types.hpp"

#include <algorithm>

namespace marketsim::gateway {

void seed_demo_liquidity(SimulatorService& svc, const domain::Symbol& symbol,
                         const domain::PriceCents reference_mid_cents) {
  using domain::OrderType;
  using domain::Side;
  constexpr domain::PriceCents kSpreadCents = 2'000;
  const matching::SubmitRequest bid{.symbol = symbol,
                                    .side = Side::Buy,
                                    .type = OrderType::Limit,
                                    .limit_price = std::max<domain::PriceCents>(1, reference_mid_cents - kSpreadCents),
                                    .quantity = 800,
                                    .owner = domain::kMarketMakerAccount};
  (void)svc.place_order(bid, std::nullopt);

  const matching::SubmitRequest ask{.symbol = symbol,
                                    .side = Side::Sell,
                                    .type = OrderType::Limit,
                                    .limit_price = reference_mid_cents + kSpreadCents,
                                    .quantity = 800,
                                    .owner = domain::kMarketMakerAccount};
  (void)svc.place_order(ask, std::nullopt);
}

}
