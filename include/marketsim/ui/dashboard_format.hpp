#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace marketsim::ui {

/// Fixed-point dollars from integer cents with thousands separators (e.g. 123456789 -> "$1,234,567.89").
[[nodiscard]] std::string format_usd_cents(std::int64_t cents);

/// Signed currency for P&L lines (uses parentheses for negatives).
[[nodiscard]] std::string format_signed_usd_cents(std::int64_t cents);

/// Compact ASCII sparkline; values are arbitrary units (e.g. mid price in cents as double).
[[nodiscard]] std::string build_sparkline_ascii(const std::vector<double>& values, int max_width);

/// Truncate UTF-8-unsafe but ASCII-safe symbol/label to max columns (terminal cells).
[[nodiscard]] std::string truncate_ascii(std::string text, std::size_t max_cols);

/// Index level with thousands separators and two decimals (e.g. "54,306.81").
[[nodiscard]] std::string format_index_value(double value);

/// Signed percentage for day change (e.g. "+0.48%" / "-1.20%").
[[nodiscard]] std::string format_signed_percent(double percent);

}
