-- Alpaca fixture processor: cancel_order
-- Draft for #26 — not yet verified against the live paper API.
-- Alpaca answers 204 No Content on successful cancel.

if ctx.status == 204 then
  return "Order cancelled."
end
return "Cancel returned HTTP " .. tostring(ctx.status) .. ": " .. tostring(ctx.body)
