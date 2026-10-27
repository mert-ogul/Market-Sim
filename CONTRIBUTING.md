# Contributing

Market Sim is small on purpose: prefer a focused change over a large redesign.

## Getting set up

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/marketsim_server
```

Run the server from the repository root so it finds `config/` and `web/`. The files under `web/` are plain HTML, CSS and JavaScript; there is no front-end build step.

## Making a change

- Open an issue first for anything large or ambiguous.
- Keep one topic per pull request.
- Add a GoogleTest for every behaviour change, next to the tests that already cover that area.
- Match the existing C++ style: run clang-format, use the naming conventions already in the tree, and keep money as integer cents in C++.
- Leave the web UI as plain HTML, CSS and JavaScript (no frameworks, bundlers or CDN libraries beyond the font link already used).
- CI must pass on your pull request.

## Good first issues

**Reserve cash for resting buy orders.** Today a resting buy does not hold cash, so several large bids can sit on the book at once. Start in `SimulatorService` and `RiskManager`. Orders leave the book through fills, cancels and the market maker's requote path, so any reserved cash has to be released in each of those places.

**Price history endpoint.** Add `GET /api/v1/candles/<SYM>` that returns one-second open/high/low/close bars. Keep a fixed-size ring per symbol and update it from `apply_mark_from_tick` when a new feed price arrives.

**An example trading bot.** Put a small script under `examples/` that places orders against a running server. Use only the Python standard library (no third-party packages).

**Thousands separators in the terminal dashboard.** `format_usd_cents` currently prints values like `$1000000.00`. Teach it to insert grouping commas so large cash and equity figures are easier to read in the TUI.

**Unused event types.** `eventbus/events.hpp` declares `OrderNew`, `OrderCancel`, `Trade`, `Quote` and `PortfolioUpdate` alongside `Tick`, but the ring mostly carries ticks. Either publish the others from the engine/service or remove the ones that will stay unused.

**Save and restore the account.** Write the trader's cash and positions to a JSON file and reload them on startup, behind a config option so the default demo behaviour stays unchanged.

## Reporting a bug

Say what you ran, what you expected, and what happened. For API problems, include the request and the response body. For UI problems, a screenshot helps.
