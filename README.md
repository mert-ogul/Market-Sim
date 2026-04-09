# Market Sim

C++20 market simulator: limit-order book, matching engine, geometric-Brownian mock feed on a single-producer/single-consumer ring buffer, portfolio and pre-trade risk checks, REST API ([cpp-httplib](https://github.com/yhirose/cpp-httplib)), and a terminal UI ([FTXUI](https://github.com/ArthurSonzogni/FTXUI)). Prices are integer cents, quantities are whole shares, simulated time is a monotonic tick counter.

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

Debug builds use `-Werror` and sanitizers by default (unless disabled above).

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

## REST quick start

Example limit buy (adjust `price_cents` to cross the book if needed):

```bash
curl -sS -X POST http://127.0.0.1:8080/api/v1/orders \
  -H 'Content-Type: application/json' \
  -d '{"symbol":"AAPL","side":"buy","type":"limit","quantity":10,"price_cents":25000}'
```

Useful reads: `GET /api/v1/portfolio`, `/api/v1/trades`, `/api/v1/quote/<SYM>`, `/api/v1/orderbook/<SYM>`.

## Configuration

`config/symbols.json` defines the symbol list and optional per-symbol fields (`initial_price_cents`, `drift`, `volatility`), plus optional `feed` settings (`seed`, `dt_years`, `pump_interval_ms`). See the checked-in file for shape.

On startup, `seed_demo_liquidity` places wide resting bid/ask around a mid derived from config so books are usually tradable immediately.

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
- **Threads:** `SimulatorRuntime` runs a producer (GBM ticks pushed to an SPSC ring) and a consumer (applies marks and risk reference from each `Tick`). HTTP runs on its own thread in server/demo binaries. The service uses **one mutex** for all mutations and reads through its public API.
- **Design choices:** Integer money avoids rounding bugs in core logic. The SPSC queue matches one writer and one reader, ticks can be dropped if the ring is full. The TUI uses `capture_view_snapshot` (structs) rather than parsing JSON.
