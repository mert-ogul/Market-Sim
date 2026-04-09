#pragma once

#include "marketsim/domain/types.hpp"

namespace marketsim::gateway {

class SimulatorService;

/// Wide resting bid/ask around @p reference_mid_cents (implementation in `demo_seed.cpp`).
void seed_demo_liquidity(SimulatorService& svc, const domain::Symbol& symbol,
                         domain::PriceCents reference_mid_cents);

}
