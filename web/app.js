"use strict";

(function () {
  const API = "/api/v1";
  const HISTORY_CAP = 600;
  const MINUS = "\u2212";

  const money = new Intl.NumberFormat("en-US", {
    style: "currency",
    currency: "USD",
  });
  const indexFmt = new Intl.NumberFormat("en-US", {
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  });

  const els = {
    indexBlock: document.getElementById("index-block"),
    indexName: document.getElementById("index-name"),
    indexValue: document.getElementById("index-value"),
    indexChange: document.getElementById("index-change"),
    cash: document.getElementById("cash"),
    equity: document.getElementById("equity"),
    pnl: document.getElementById("pnl"),
    statusLine: document.getElementById("status-line"),
    board: document.getElementById("board"),
    companyTitle: document.getElementById("company-title"),
    focusLast: document.getElementById("focus-last"),
    focusChange: document.getElementById("focus-change"),
    chart: document.getElementById("chart"),
    orderbookBody: document.getElementById("orderbook-body"),
    orderForm: document.getElementById("order-form"),
    shares: document.getElementById("shares"),
    limitPrice: document.getElementById("limit-price"),
    submit: document.getElementById("submit-order"),
    formResult: document.getElementById("form-result"),
    positionsBody: document.getElementById("positions-body"),
    ordersBody: document.getElementById("orders-body"),
    tradesBody: document.getElementById("trades-body"),
  };

  const state = {
    symbols: [],
    bySymbol: new Map(),
    selected: null,
    boardBuilt: false,
    history: new Map(),
    openBySymbol: new Map(),
    index: null,
  };

  const ERROR_WORDS = {
    insufficient_cash: "Not enough cash for this order.",
    order_exceeds_max_size: "That is more shares than one order may hold.",
    position_limit_exceeded: "This order would take your position past the limit.",
    market_no_liquidity: "Nobody is quoting this stock right now. Try a limit order.",
    unknown_symbol_reference: "This stock has no price yet. Wait a moment and try again.",
  };

  function centsToDollars(cents) {
    return cents / 100;
  }

  function formatMoney(cents) {
    if (cents == null) {
      return "—";
    }
    return money.format(centsToDollars(cents));
  }

  function formatSignedMoney(dollars) {
    const abs = indexFmt.format(Math.abs(dollars));
    if (dollars > 0) {
      return "+" + abs;
    }
    if (dollars < 0) {
      return MINUS + abs;
    }
    return abs;
  }

  function formatSignedPercent(pct) {
    const abs = Math.abs(pct).toFixed(2) + "%";
    if (pct > 0) {
      return "+" + abs;
    }
    if (pct < 0) {
      return MINUS + abs;
    }
    return "+" + abs;
  }

  function changePct(lastCents, openCents) {
    if (lastCents == null || openCents == null || openCents === 0) {
      return null;
    }
    return ((lastCents - openCents) / openCents) * 100;
  }

  function setReachable(ok) {
    els.statusLine.hidden = ok;
  }

  async function fetchJson(path) {
    const res = await fetch(API + path);
    let body = null;
    try {
      body = await res.json();
    } catch (_) {
      body = null;
    }
    return { ok: res.ok, status: res.status, body };
  }

  function selectedSide() {
    const el = els.orderForm.querySelector('input[name="side"]:checked');
    return el ? el.value : "buy";
  }

  function selectedType() {
    const el = els.orderForm.querySelector('input[name="type"]:checked');
    return el ? el.value : "market";
  }

  function updateSubmitLabel() {
    const side = selectedSide();
    const type = selectedType();
    const shares = els.shares.value.trim() || "0";
    const symbol = state.selected || "—";
    const isBuy = side === "buy";
    els.submit.classList.toggle("buy", isBuy);
    els.submit.classList.toggle("sell", !isBuy);

    let label;
    if (type === "market") {
      label = (isBuy ? "Buy " : "Sell ") + shares + " " + symbol + " at market";
    } else {
      const px = els.limitPrice.value.trim();
      const pxText = px === "" ? "$—" : money.format(Number(px));
      label = (isBuy ? "Buy " : "Sell ") + shares + " " + symbol + " at " + pxText;
    }
    els.submit.textContent = label;
  }

  function syncTypeControls() {
    const isLimit = selectedType() === "limit";
    els.limitPrice.disabled = !isLimit;
    if (isLimit && els.limitPrice.value.trim() === "" && state.selected) {
      const row = state.bySymbol.get(state.selected);
      if (row && row.last_price_cents != null) {
        els.limitPrice.value = centsToDollars(row.last_price_cents).toFixed(2);
      }
    }
    updateSubmitLabel();
  }

  function selectSymbol(symbol, pushHash) {
    if (!symbol) {
      return;
    }
    state.selected = symbol;
    if (pushHash !== false) {
      history.replaceState(null, "", "#" + encodeURIComponent(symbol));
    }
    for (const btn of els.board.querySelectorAll(".tile")) {
      btn.setAttribute("aria-pressed", btn.dataset.symbol === symbol ? "true" : "false");
    }
    renderFocus();
    drawChart();
    updateSubmitLabel();
    refreshOrderbook();
  }

  function appendHistory(symbol, lastCents) {
    if (lastCents == null) {
      return;
    }
    let series = state.history.get(symbol);
    if (!series) {
      series = [];
      state.history.set(symbol, series);
    }
    series.push(lastCents);
    if (series.length > HISTORY_CAP) {
      series.splice(0, series.length - HISTORY_CAP);
    }
  }

  function paintTile(btn, row) {
    const pct = changePct(row.last_price_cents, row.open_price_cents);
    const changeEl = btn.querySelector(".tile-change");
    const priceEl = btn.querySelector(".tile-price");
    priceEl.textContent = formatMoney(row.last_price_cents);

    let strength = 0;
    let tone = "var(--shade)";
    if (pct == null) {
      changeEl.textContent = "—";
      changeEl.className = "tile-change";
    } else {
      changeEl.textContent = formatSignedPercent(pct);
      changeEl.className = "tile-change " + (pct < 0 ? "down" : pct > 0 ? "up" : "");
      strength = Math.min(Math.abs(pct) / 3, 1);
      const toward = pct < 0 ? "var(--down)" : "var(--up)";
      tone = "color-mix(in srgb, " + toward + " " + (strength * 100).toFixed(2) + "%, var(--shade))";
    }
    btn.style.background = tone;
    btn.classList.toggle("bright", strength > 0.55);

    const priceLabel = row.last_price_cents == null ? "no price" : formatMoney(row.last_price_cents);
    const changeLabel = pct == null ? "no change" : formatSignedPercent(pct);
    btn.setAttribute("aria-label", row.name + ", " + priceLabel + ", " + changeLabel);
  }

  function buildBoard(symbols) {
    els.board.replaceChildren();
    for (const row of symbols) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "tile";
      btn.dataset.symbol = row.symbol;
      btn.setAttribute("aria-pressed", "false");

      const sym = document.createElement("span");
      sym.className = "tile-symbol";
      sym.textContent = row.symbol;

      const change = document.createElement("span");
      change.className = "tile-change";

      const price = document.createElement("span");
      price.className = "tile-price";

      btn.append(sym, change, price);
      btn.addEventListener("click", function () {
        selectSymbol(row.symbol);
      });
      els.board.appendChild(btn);
      paintTile(btn, row);
    }
    state.boardBuilt = true;
  }

  function updateBoard(symbols) {
    if (!state.boardBuilt) {
      buildBoard(symbols);
      return;
    }
    for (const row of symbols) {
      const btn = els.board.querySelector('.tile[data-symbol="' + CSS.escape(row.symbol) + '"]');
      if (btn) {
        paintTile(btn, row);
      }
    }
  }

  function renderIndex(index) {
    if (!index) {
      els.indexBlock.hidden = true;
      return;
    }
    els.indexBlock.hidden = false;
    els.indexName.textContent = index.name || "";
    if (index.value == null) {
      els.indexValue.textContent = "—";
      els.indexChange.textContent = "";
      els.indexChange.className = "index-change";
      return;
    }
    els.indexValue.textContent = indexFmt.format(index.value);
    document.title = indexFmt.format(index.value) + " " + (index.name || "") + " | Market Sim";

    if (index.open_value == null || index.open_value === 0) {
      els.indexChange.textContent = "";
      els.indexChange.className = "index-change";
      return;
    }
    const delta = index.value - index.open_value;
    const pct = (delta / index.open_value) * 100;
    els.indexChange.textContent = formatSignedMoney(delta) + "  " + formatSignedPercent(pct);
    els.indexChange.className = "index-change " + (delta < 0 ? "down" : delta > 0 ? "up" : "");
  }

  function renderFocus() {
    const row = state.bySymbol.get(state.selected);
    if (!row) {
      els.companyTitle.textContent = "—";
      els.focusLast.textContent = "—";
      els.focusChange.textContent = "";
      return;
    }
    els.companyTitle.textContent = row.name + " (" + row.symbol + ")";
    els.focusLast.textContent = formatMoney(row.last_price_cents);
    const pct = changePct(row.last_price_cents, row.open_price_cents);
    if (pct == null) {
      els.focusChange.textContent = "";
      els.focusChange.className = "";
    } else {
      els.focusChange.textContent = formatSignedPercent(pct);
      els.focusChange.className = pct < 0 ? "down" : pct > 0 ? "up" : "";
    }
  }

  function clearTable(tbody, colspan, message) {
    tbody.replaceChildren();
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.colSpan = colspan;
    td.className = "empty";
    td.textContent = message;
    tr.appendChild(td);
    tbody.appendChild(tr);
  }

  function renderOrderbook(book) {
    const asks = (book && book.asks ? book.asks.slice() : []).slice(0, 5);
    asks.sort(function (a, b) {
      return b.price_cents - a.price_cents;
    });
    const bids = (book && book.bids ? book.bids.slice() : []).slice(0, 5);

    if (asks.length === 0 && bids.length === 0) {
      clearTable(els.orderbookBody, 3, "No orders in the book yet.");
      return;
    }

    els.orderbookBody.replaceChildren();
    for (const level of asks) {
      const tr = document.createElement("tr");
      const bidSize = document.createElement("td");
      bidSize.textContent = "";
      const price = document.createElement("td");
      price.className = "ask-px";
      price.textContent = formatMoney(level.price_cents);
      const askSize = document.createElement("td");
      askSize.textContent = String(level.total_quantity);
      tr.append(bidSize, price, askSize);
      els.orderbookBody.appendChild(tr);
    }
    for (const level of bids) {
      const tr = document.createElement("tr");
      const bidSize = document.createElement("td");
      bidSize.textContent = String(level.total_quantity);
      const price = document.createElement("td");
      price.className = "bid-px";
      price.textContent = formatMoney(level.price_cents);
      const askSize = document.createElement("td");
      askSize.textContent = "";
      tr.append(bidSize, price, askSize);
      els.orderbookBody.appendChild(tr);
    }
  }

  function renderPositions(positions) {
    const open = (positions || []).filter(function (p) {
      return p.quantity !== 0;
    });
    if (open.length === 0) {
      clearTable(els.positionsBody, 4, "No positions yet. Buy a stock with the order form above.");
      return;
    }
    els.positionsBody.replaceChildren();
    for (const p of open) {
      const tr = document.createElement("tr");
      const stock = document.createElement("td");
      stock.textContent = p.symbol;
      const shares = document.createElement("td");
      shares.textContent = String(p.quantity);
      const avg = document.createElement("td");
      avg.textContent = formatMoney(p.avg_entry_cents);
      const pnl = document.createElement("td");
      const pnlCents = p.unrealized_pnl_cents || 0;
      pnl.textContent = formatMoney(pnlCents);
      if (pnlCents > 0) {
        pnl.className = "up";
      } else if (pnlCents < 0) {
        pnl.className = "down";
      }
      tr.append(stock, shares, avg, pnl);
      els.positionsBody.appendChild(tr);
    }
  }

  function renderOpenOrders(orders) {
    if (!orders || orders.length === 0) {
      clearTable(
        els.ordersBody,
        3,
        "No open orders. Limit orders wait here until the price reaches them."
      );
      return;
    }
    els.ordersBody.replaceChildren();
    for (const o of orders) {
      const tr = document.createElement("tr");
      const desc = document.createElement("td");
      const sideWord = o.side === "buy" ? "Buy" : "Sell";
      desc.textContent = sideWord + " " + o.remaining + " " + o.symbol;

      const px = document.createElement("td");
      px.textContent = formatMoney(o.price_cents);

      const action = document.createElement("td");
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "cancel-btn";
      btn.textContent = "Cancel";
      btn.setAttribute("aria-label", "Cancel order " + o.order_id);
      btn.addEventListener("click", function () {
        cancelOrder(o.order_id);
      });
      action.appendChild(btn);
      tr.append(desc, px, action);
      els.ordersBody.appendChild(tr);
    }
  }

  function renderTrades(trades) {
    const rows = (trades || []).slice(0, 8);
    if (rows.length === 0) {
      clearTable(els.tradesBody, 2, "No trades yet.");
      return;
    }
    els.tradesBody.replaceChildren();
    for (const t of rows) {
      const tr = document.createElement("tr");
      const desc = document.createElement("td");
      const verb = t.side === "buy" ? "Bought" : "Sold";
      desc.textContent = verb + " " + t.quantity + " " + t.symbol;
      const px = document.createElement("td");
      px.textContent = formatMoney(t.price_cents);
      tr.append(desc, px);
      els.tradesBody.appendChild(tr);
    }
  }

  function drawChart() {
    const canvas = els.chart;
    const series = state.history.get(state.selected) || [];
    const dpr = window.devicePixelRatio || 1;
    const cssWidth = canvas.clientWidth || canvas.parentElement.clientWidth || 800;
    const cssHeight = 260;
    canvas.width = Math.round(cssWidth * dpr);
    canvas.height = Math.round(cssHeight * dpr);
    canvas.style.width = cssWidth + "px";
    canvas.style.height = cssHeight + "px";

    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cssWidth, cssHeight);

    const labelW = 64;
    const padL = 8;
    const padR = 8;
    const padT = 12;
    const padB = 12;
    const plotW = cssWidth - padL - padR - labelW;
    const plotH = cssHeight - padT - padB;

    const openCents = state.openBySymbol.get(state.selected);
    if (series.length === 0) {
      return;
    }

    let lo = series[0];
    let hi = series[0];
    for (const v of series) {
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    if (openCents != null) {
      lo = Math.min(lo, openCents);
      hi = Math.max(hi, openCents);
    }
    const span = hi - lo || 1;
    const pad = span * 0.1;
    lo -= pad;
    hi += pad;

    function yFor(cents) {
      return padT + ((hi - cents) / (hi - lo)) * plotH;
    }

    function xFor(i) {
      if (series.length === 1) {
        return padL;
      }
      return padL + (i / (series.length - 1)) * plotW;
    }

    const inkSoft = getComputedStyle(document.documentElement).getPropertyValue("--ink-soft").trim();
    const rule = getComputedStyle(document.documentElement).getPropertyValue("--rule").trim();
    const ink = getComputedStyle(document.documentElement).getPropertyValue("--ink").trim();

    const gridVals = [lo + (hi - lo) * 0.75, lo + (hi - lo) * 0.5, lo + (hi - lo) * 0.25];
    const openY = openCents != null ? yFor(openCents) : null;

    ctx.strokeStyle = rule;
    ctx.lineWidth = 1;
    ctx.font = "12px Archivo, system-ui, sans-serif";
    ctx.fillStyle = inkSoft;
    ctx.textAlign = "left";
    ctx.textBaseline = "middle";

    for (const gv of gridVals) {
      const y = yFor(gv);
      ctx.beginPath();
      ctx.moveTo(padL, y);
      ctx.lineTo(padL + plotW, y);
      ctx.stroke();
      if (openY == null || Math.abs(y - openY) >= 16) {
        ctx.fillText(money.format(centsToDollars(gv)), padL + plotW + 8, y);
      }
    }

    if (openCents != null) {
      ctx.save();
      ctx.strokeStyle = inkSoft;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(padL, openY);
      ctx.lineTo(padL + plotW, openY);
      ctx.stroke();
      ctx.restore();
      ctx.fillStyle = inkSoft;
      ctx.fillText("Open", padL + plotW + 8, openY);
    }

    ctx.strokeStyle = ink;
    ctx.lineWidth = 1.75;
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    ctx.beginPath();
    for (let i = 0; i < series.length; ++i) {
      const x = xFor(i);
      const y = yFor(series[i]);
      if (i === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }
    ctx.stroke();
  }

  async function refreshOrderbook() {
    if (!state.selected) {
      return;
    }
    try {
      const book = await fetchJson("/orderbook/" + encodeURIComponent(state.selected));
      if (!book.ok) {
        setReachable(false);
        return;
      }
      setReachable(true);
      renderOrderbook(book.body);
    } catch (_) {
      setReachable(false);
    }
  }

  async function marketTick() {
    try {
      const [symbolsRes, indexRes] = await Promise.all([
        fetchJson("/symbols"),
        fetchJson("/index"),
      ]);
      if (!symbolsRes.ok) {
        setReachable(false);
        return;
      }
      setReachable(true);

      const symbols = (symbolsRes.body && symbolsRes.body.symbols) || [];
      state.symbols = symbols;
      state.bySymbol = new Map(symbols.map(function (s) {
        return [s.symbol, s];
      }));
      for (const s of symbols) {
        if (s.open_price_cents != null) {
          state.openBySymbol.set(s.symbol, s.open_price_cents);
        }
        appendHistory(s.symbol, s.last_price_cents);
      }

      updateBoard(symbols);

      if (indexRes.status === 404) {
        state.index = null;
        renderIndex(null);
      } else if (indexRes.ok) {
        state.index = indexRes.body;
        renderIndex(indexRes.body);
      }

      if (!state.selected) {
        const hash = decodeURIComponent((location.hash || "").replace(/^#/, ""));
        const fromHash = hash && state.bySymbol.has(hash) ? hash : null;
        const initial = fromHash || (symbols[0] && symbols[0].symbol);
        if (initial) {
          selectSymbol(initial, !fromHash);
        }
      } else {
        renderFocus();
        drawChart();
        await refreshOrderbook();
      }
    } catch (_) {
      setReachable(false);
    }
  }

  async function accountTick() {
    try {
      const [portfolio, orders, trades] = await Promise.all([
        fetchJson("/portfolio"),
        fetchJson("/orders"),
        fetchJson("/trades"),
      ]);
      if (!portfolio.ok || !orders.ok || !trades.ok) {
        setReachable(false);
        return;
      }
      setReachable(true);

      const p = portfolio.body;
      els.cash.textContent = formatMoney(p.cash_cents);
      els.equity.textContent = formatMoney(p.equity_cents);
      const pnlCents = (p.total_realized_pnl_cents || 0) + (p.total_unrealized_pnl_cents || 0);
      els.pnl.textContent = formatMoney(pnlCents);
      els.pnl.className = pnlCents > 0 ? "up" : pnlCents < 0 ? "down" : "";

      renderPositions(p.positions);
      renderOpenOrders((orders.body && orders.body.orders) || []);
      renderTrades((trades.body && trades.body.trades) || []);
    } catch (_) {
      setReachable(false);
    }
  }

  function loop(fn, delayMs) {
    let stopped = false;
    async function step() {
      if (stopped) {
        return;
      }
      await fn();
      if (!stopped) {
        setTimeout(step, delayMs);
      }
    }
    setTimeout(step, 0);
    return function stop() {
      stopped = true;
    };
  }

  function explainError(body) {
    if (!body) {
      return "Something went wrong.";
    }
    const code = body.error;
    if (code && ERROR_WORDS[code]) {
      return ERROR_WORDS[code];
    }
    if (body.message) {
      return body.message;
    }
    return "Something went wrong.";
  }

  function averageFillCents(trades) {
    if (!trades || trades.length === 0) {
      return null;
    }
    let notional = 0;
    let qty = 0;
    for (const t of trades) {
      notional += t.price_cents * t.quantity;
      qty += t.quantity;
    }
    if (qty === 0) {
      return null;
    }
    return Math.round(notional / qty);
  }

  async function cancelOrder(id) {
    try {
      const res = await fetch(API + "/orders/" + id, { method: "DELETE" });
      let body = null;
      try {
        body = await res.json();
      } catch (_) {
        body = null;
      }
      if (!res.ok) {
        els.formResult.className = "form-result error";
        els.formResult.textContent = explainError(body);
        setReachable(res.status !== 0);
        return;
      }
      setReachable(true);
      els.formResult.className = "form-result";
      els.formResult.textContent = "Cancelled order " + id + ".";
      await accountTick();
    } catch (_) {
      setReachable(false);
      els.formResult.className = "form-result error";
      els.formResult.textContent = "Can't reach the simulator.";
    }
  }

  els.orderForm.addEventListener("input", function () {
    if (selectedType() === "market") {
      els.limitPrice.disabled = true;
    } else {
      els.limitPrice.disabled = false;
    }
    updateSubmitLabel();
  });

  els.orderForm.addEventListener("change", function (ev) {
    if (ev.target && ev.target.name === "type") {
      syncTypeControls();
    } else {
      updateSubmitLabel();
    }
  });

  els.orderForm.addEventListener("submit", async function (ev) {
    ev.preventDefault();
    els.formResult.className = "form-result";
    els.formResult.textContent = "";

    const sharesRaw = els.shares.value.trim();
    const shares = Number(sharesRaw);
    if (!Number.isInteger(shares) || shares <= 0) {
      els.formResult.className = "form-result error";
      els.formResult.textContent = "Enter a whole number of shares above 0.";
      return;
    }

    const side = selectedSide();
    const type = selectedType();
    const payload = {
      symbol: state.selected,
      side: side,
      type: type,
      quantity: shares,
    };

    if (type === "limit") {
      const dollars = Number(els.limitPrice.value);
      if (!(dollars > 0)) {
        els.formResult.className = "form-result error";
        els.formResult.textContent = "Enter a limit price above $0.";
        return;
      }
      payload.price_cents = Math.round(dollars * 100);
    }

    try {
      const res = await fetch(API + "/orders", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      let body = null;
      try {
        body = await res.json();
      } catch (_) {
        body = null;
      }
      if (!res.ok) {
        els.formResult.className = "form-result error";
        els.formResult.textContent = explainError(body);
        setReachable(true);
        return;
      }
      setReachable(true);

      const trades = body.trades || [];
      const filledQty = trades.reduce(function (sum, t) {
        return sum + t.quantity;
      }, 0);
      const avg = averageFillCents(trades);
      const verb = side === "buy" ? "Bought" : "Sold";
      const parts = [];
      if (filledQty > 0 && avg != null) {
        parts.push(verb + " " + filledQty + " " + state.selected + " at " + formatMoney(avg) + ".");
      } else {
        parts.push("Order accepted.");
      }
      const resting = body.resting_quantity || 0;
      if (resting > 0 && payload.price_cents != null) {
        parts.push(resting + " shares are waiting at " + formatMoney(payload.price_cents) + ".");
      } else if (resting > 0) {
        parts.push(resting + " shares are waiting.");
      }
      if (body.self_trade_cancelled_order_ids && body.self_trade_cancelled_order_ids.length > 0) {
        parts.push("Your opposite order was cancelled so you don't trade with yourself.");
      }
      els.formResult.className = "form-result";
      els.formResult.textContent = parts.join(" ");
      await accountTick();
    } catch (_) {
      setReachable(false);
      els.formResult.className = "form-result error";
      els.formResult.textContent = "Can't reach the simulator.";
    }
  });

  window.addEventListener("hashchange", function () {
    const hash = decodeURIComponent((location.hash || "").replace(/^#/, ""));
    if (hash && state.bySymbol.has(hash) && hash !== state.selected) {
      selectSymbol(hash, false);
    }
  });

  window.addEventListener("resize", function () {
    drawChart();
  });

  updateSubmitLabel();
  loop(marketTick, 500);
  loop(accountTick, 1000);
})();
