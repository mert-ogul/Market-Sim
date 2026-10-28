#pragma once

namespace marketsim::domain {

/// Simulated non-trader accounts that fire market orders against the book each feed tick.
struct BotSettings {
  bool enabled{false};
  /// Scales how often each rule fires per tick; 0 turns activity off without disabling the feature.
  double activity{1.0};
};

}
