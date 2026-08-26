-- Alpaca fixture processor: submit_order
-- Draft for #26 — not yet verified against the live paper API.
-- The returned string becomes the tool result. Position bookkeeping
-- (strategy book writes) is deliberately NOT done here in the fixture;
-- whether order actions may write state is an #22/#26 design question.

local o = ctx.json or {}
return string.format(
  "Order %s: %s %s %s (%s)",
  o.id or "?",
  o.side or "?",
  o.qty or o.notional or "?",
  o.symbol or "?",
  o.status or "?"
)
