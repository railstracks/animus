-- Alpaca fixture processor: browse_markets
-- Draft for #26 — not yet verified against the live paper API.
-- Proposed processor convention for #22: the interpreter calls
--   process(ctx) -> string
-- with ctx = { status = number, body = string, json = table|nil,
--              params = table, state = table }

local assets = ctx.json or {}

local lines = { string.format("%d active assets", #assets) }
for i, a in ipairs(assets) do
  if i > 25 then
    lines[#lines + 1] = "... (truncated)"
    break
  end
  lines[#lines + 1] = string.format("%-6s %-8s %s", a.symbol or "?", a.exchange or "?", a.name or "")
end
return table.concat(lines, "\n")
