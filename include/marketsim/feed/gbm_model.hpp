#pragma once

#include <cmath>

namespace marketsim::feed {

/// One-step multiplicative factor for Geometric Brownian Motion in continuous time:
/// S' = S * exp((μ - σ²/2)Δt + σ√Δt Z), Z ~ N(0,1).
/// μ and σ are annualized; Δt is in years (e.g. one second / seconds_per_year).
[[nodiscard]] inline double gbm_multiplicative_factor(double mu_annual, double sigma_annual,
                                                      double dt_years, double z) noexcept {
  const double m = (mu_annual - 0.5 * sigma_annual * sigma_annual) * dt_years;
  const double s = sigma_annual * std::sqrt(dt_years);
  return std::exp(m + s * z);
}

}
