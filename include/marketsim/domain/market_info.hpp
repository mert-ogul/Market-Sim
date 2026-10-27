#pragma once

#include "marketsim/domain/types.hpp"

#include <string>
#include <unordered_map>

namespace marketsim::domain {

/// Company names and the price-weighted index reported by the market endpoints and dashboards.
struct MarketInfo {
  std::unordered_map<Symbol, std::string> company_names{};
  std::string index_name{};
  /// Index value = sum of last prices in dollars / divisor, the way the Dow is computed.
  /// 0 disables the index.
  double index_divisor{0.0};
};

}
