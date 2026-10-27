# Market Sim

C++20 market simulator: limit-order book, matching engine, geometric-Brownian mock feed on a single-producer/single-consumer ring buffer, portfolio and pre-trade risk checks, a market maker that quotes around the feed, REST API ([cpp-httplib](https://github.com/yhirose/cpp-httplib)), and a terminal UI ([FTXUI](https://github.com/ArthurSonzogni/FTXUI)). Prices are integer cents, quantities are whole shares, simulated time is a monotonic tick counter.

You trade as a single account against a simulated market maker; every feed tick replaces its bid and ask around the new price, so the book follows the feed and your resting limit orders fill when the price moves through them.

## Requirements

- CMake 3.20+
- C++20 compiler (Clang 14+ or GCC 11+)
- Git (FetchContent pulls dependencies)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Release:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

CMake options:

- `-DMARKETSIM_WERROR=OFF` — do not treat warnings as errors.
- `-DMARKETSIM_SANITIZE=OFF` — disable AddressSanitizer and UBSan in Debug builds.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Debug builds use `-Werror` and sanitizers by default (unless disabled above). CI runs the same build and tests with GCC and Clang on pushes to main and pull requests, plus a Release build.

## Small demo binary

```bash
./build/marketsim
```

## Executables

| Binary | HTTP | Terminal UI | Notes |
|--------|------|-------------|--------|
| `marketsim_server` | yes (`0.0.0.0:8080` by default) | no | Feed threads + REST. |
| `marketsim_dashboard` | no | yes | Same runtime, trade via UI. |
| `marketsim_demo` | yes (`127.0.0.1`) | yes | REST and UI share one `SimulatorService`. |

Run long-lived binaries from the **repository root** (or pass an explicit path) so `config/symbols.json` resolves. If the file is missing or invalid, built-in defaults apply (same symbol set as `GbmFeed::register_default_symbols()`).

```bash
./build/marketsim_server [port] [path/to/symbols.json]
./build/marketsim_dashboard [SYMBOL] [path/to/symbols.json]
./build/marketsim_demo [port] [path/to/symbols.json] [ui_focus_symbol]
```

Stop with **Ctrl+C** (SIGINT/SIGTERM): HTTP thread stops and joins, then feed threads stop.

## REST API

| Method | Path | Purpose |
|--------|------|---------|
| `POST` | `/api/v1/orders` | Place an order. Body: `symbol`, `side` (`buy`/`sell`), `type` (`limit`/`market`), `quantity`, `price_cents` for limits, optional `client_id`. |
| `DELETE` | `/api/v1/orders/<id>` | Cancel a resting order. |
| `GET` | `/api/v1/orders` | List your resting orders. |
| `GET` | `/api/v1/symbols` | Configured symbols and last feed prices. |
| `GET` | `/api/v1/quote/<SYM>` | Top of book; includes `last_price_cents`. |
| `GET` | `/api/v1/orderbook/<SYM>` | Depth snapshot. |
| `GET` | `/api/v1/portfolio` | Cash, equity, positions. |
| `GET` | `/api/v1/trades` | Trade tape (`side` = your side, `aggressive_side` = the side that crossed the spread). |

Errors return JSON with a stable `"error"` code and a `"message"`; `400` malformed request, `404` unknown symbol/order/route, `422` rejected by risk or the engine.

```bash
curl -sS -X POST http://127.0.0.1:8080/api/v1/orders \
  -H 'Content-Type: application/json' \
  -d '{"symbol":"AAPL","side":"buy","type":"market","quantity":10}'
```

Crossing your own resting order cancels that resting order instead of trading (self-trade prevention); successful place responses include `self_trade_cancelled_order_ids` (possibly empty).

## Terminal dashboard

Tab moves between controls, Enter presses a button, the side and type buttons flip Buy/Sell and Limit/Market, prices in cents, `[` and `]` switch symbols, `q` quits.

## Configuration

`config/symbols.json` sections:

- **symbols** — built-in AAPL, GOOG, MSFT, TSLA may be plain strings; any other ticker needs `initial_price_cents`, optionally `drift` and `volatility`, otherwise it is skipped with a warning.
- **feed** — `seed`, `dt_years`, `pump_interval_ms`.
- **account** — `cash_cents` and the limits `max_order_size`, `max_position_per_symbol`, `min_price_cents`, `max_price_cents`, `price_collar_cents`; `0` disables a limit.
- **market_maker** — `enabled`, `half_spread_bps`, `quote_size`.

Missing sections use built-in defaults. See the checked-in file for shape.

## Known limitations

Resting buy orders do not reserve cash; market buys are checked against the latest feed price but fill at the maker's ask (half a spread higher); one trader account and no persistence, state resets on restart.

## Project layout

| Path | Purpose |
|------|---------|
| `include/marketsim/` | Headers: domain, order book, matching, event bus, feed, portfolio, config, runtime, gateway, view, UI |
| `src/` | Implementations and entry points |
| `tests/` | GoogleTest suites |
| `config/` | Runtime JSON |
| `cmake/` | Shared CMake helpers |

## Architecture (short)

- **Layers:** `domain` types and market structs → `orderbook` / `matching` → `portfolio` + `risk` → `gateway::SimulatorService` (HTTP façade) → `view` read models for the UI → `ui` formatting and FTXUI.
- **Threads:** `SimulatorRuntime` runs a producer (GBM ticks pushed to an SPSC ring) and a consumer (applies marks and risk reference from each `Tick`; the consumer also requotes the market maker). HTTP runs on its own thread in server/demo binaries. The service uses **one mutex** for all mutations and reads through its public API.
- **Design choices:** Integer money avoids rounding bugs in core logic. The SPSC queue matches one writer and one reader, ticks can be dropped if the ring is full. The TUI uses `capture_view_snapshot` (structs) rather than parsing JSON.
