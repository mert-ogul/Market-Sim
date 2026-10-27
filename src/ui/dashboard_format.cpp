#include "marketsim/ui/dashboard_format.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace marketsim::ui {

namespace {

[[nodiscard]] std::string format_abs_cents_core(std::int64_t cents, bool negative) {
  const std::int64_t abs_c = cents >= 0 ? cents : -cents;
  const std::int64_t dollars = abs_c / 100;
  const std::int64_t frac = abs_c % 100;
  std::ostringstream os;
  if (negative) {
    os << '-';
  }
  os << '$' << dollars << '.' << std::setw(2) << std::setfill('0') << frac;
  return std::move(os).str();
}

}

std::string format_usd_cents(const std::int64_t cents) {
  return format_abs_cents_core(cents, cents < 0);
}

std::string format_signed_usd_cents(const std::int64_t cents) {
  if (cents < 0) {
    return std::string("(") + format_abs_cents_core(cents, true).substr(1) + ")";
  }
  return format_abs_cents_core(cents, false);
}

std::string build_sparkline_ascii(const std::vector<double>& values, const int max_width) {
  if (max_width <= 0 || values.empty()) {
    return {};
  }
  static constexpr const char* kBlocks = " ._-~=#%@";
  static constexpr int kLevels = 9;

  const std::size_t n = values.size();
  const int w = std::min(max_width, static_cast<int>(n));
  if (w <= 0) {
    return {};
  }

  double lo = values.front();
  double hi = values.front();
  for (double v : values) {
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  if (hi <= lo) {
    return std::string(static_cast<std::size_t>(w), kBlocks[kLevels / 2]);
  }

  std::string out;
  out.reserve(static_cast<std::size_t>(w));
  if (n == 1) {
    return std::string(static_cast<std::size_t>(w), kBlocks[kLevels / 2]);
  }
  const long denom = static_cast<long>(w - 1);
  for (int i = 0; i < w; ++i) {
    const std::size_t idx = static_cast<std::size_t>((i * static_cast<long>(n - 1)) / denom);
    const double v = values[idx];
    const double t = (v - lo) / (hi - lo);
    const int level = static_cast<int>(std::lround(t * static_cast<double>(kLevels - 1)));
    const int clamped = std::clamp(level, 0, kLevels - 1);
    out.push_back(kBlocks[static_cast<std::size_t>(clamped)]);
  }
  return out;
}

std::string truncate_ascii(std::string text, const std::size_t max_cols) {
  if (text.size() <= max_cols) {
    return text;
  }
  if (max_cols <= 3) {
    return text.substr(0, max_cols);
  }
  text.resize(max_cols - 3);
  text.append("...");
  return text;
}

std::string format_index_value(const double value) {
  const bool negative = value < 0.0;
  const long long hundredths = std::llround(std::abs(value) * 100.0);
  const long long whole = hundredths / 100;
  const int frac = static_cast<int>(hundredths % 100);

  const std::string digits = std::to_string(whole);
  std::string with_commas;
  with_commas.reserve(digits.size() + digits.size() / 3);
  const int n = static_cast<int>(digits.size());
  for (int i = 0; i < n; ++i) {
    if (i > 0 && (n - i) % 3 == 0) {
      with_commas.push_back(',');
    }
    with_commas.push_back(digits[static_cast<std::size_t>(i)]);
  }

  std::ostringstream os;
  if (negative) {
    os << '-';
  }
  os << with_commas << '.' << std::setw(2) << std::setfill('0') << frac;
  return std::move(os).str();
}

std::string format_signed_percent(const double percent) {
  std::ostringstream os;
  os << std::showpos << std::fixed << std::setprecision(2) << percent << '%';
  return std::move(os).str();
}

}
