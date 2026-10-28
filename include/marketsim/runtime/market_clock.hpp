#pragma once

#include "marketsim/domain/market_data.hpp"

namespace marketsim::runtime {

/// Seconds of trading in a year of 252 sessions of 6.5 hours (for converting dt_years to market time).
inline constexpr double kTradingSecondsPerYear = 252.0 * 6.5 * 3600.0;

/// Advances a trading day from 9:30 to 16:00, then waits closed_ticks before the next open.
class MarketClock {
public:
  static constexpr int kOpenMinute = 570;
  static constexpr int kCloseMinute = 960;

  MarketClock(double market_seconds_per_tick, int closed_ticks)
      : market_seconds_per_tick_(market_seconds_per_tick), closed_ticks_(closed_ticks) {}

  void advance() {
    if (open_) {
      seconds_into_session_ += market_seconds_per_tick_;
      if (seconds_into_session_ >= kSessionSeconds) {
        open_ = false;
        remaining_closed_ticks_ = closed_ticks_;
        if (remaining_closed_ticks_ == 0) {
          begin_next_day();
        }
      }
      return;
    }
    --remaining_closed_ticks_;
    if (remaining_closed_ticks_ <= 0) {
      begin_next_day();
    }
  }

  [[nodiscard]] domain::MarketClockState state() const {
    domain::MarketClockState s;
    s.day = day_;
    s.open = open_;
    // While closed the wall stays on the close so clients do not see time keep moving overnight.
    s.minute_of_day =
        open_ ? kOpenMinute + static_cast<int>(seconds_into_session_ / 60.0) : kCloseMinute;
    return s;
  }

private:
  static constexpr double kSessionSeconds = 6.5 * 3600.0;

  void begin_next_day() {
    ++day_;
    open_ = true;
    seconds_into_session_ = 0.0;
    remaining_closed_ticks_ = 0;
  }

  double market_seconds_per_tick_{};
  int closed_ticks_{};
  int day_{1};
  double seconds_into_session_{0.0};
  bool open_{true};
  int remaining_closed_ticks_{0};
};

}
