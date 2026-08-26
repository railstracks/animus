-- Alpaca fixture processor: get_bars
-- Draft for #26 — not yet verified against the live paper API.

local json = ctx.json or {}
local bars = json.bars or {}

if #bars == 0 then
  return "No bars returned."
end

local hi, lo = -math.huge, math.huge
for _, b in ipairs(bars) do
  if b.h and b.h > hi then hi = b.h end
  if b.l and b.l < lo then lo = b.l end
end

local first, last = bars[1], bars[#bars]
return string.format(
  "%d bars  %s .. %s\nopen %.2f -> close %.2f   high %.2f   low %.2f",
  #bars,
  first.t or "?",
  last.t or "?",
  first.o or 0,
  last.c or 0,
  hi == -math.huge and 0 or hi,
  lo == math.huge and 0 or lo
)
