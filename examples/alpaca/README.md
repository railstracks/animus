# Alpaca API package — draft fixture

The worked example for the v1 manifest schema (#21) and the first fixture for the
pilot (#26). Scope matches the pilot: orders + markets + OHLCV + strategy book.
"Wrap all of Alpaca's API" is a follow-up, not the pilot.

**Status: draft.** The manifest validates against
[`schemas/api-package-manifest-v1.json`](../../schemas/api-package-manifest-v1.json),
but neither the endpoints nor the processors have been verified against the live
paper-trading API yet — that verification happens in #26.

## Processor convention (proposal for #22)

The interpreter calls `process(ctx) -> string` with:

- `ctx.status` — HTTP status number
- `ctx.body` — raw response body string
- `ctx.json` — decoded body table, or `nil`
- `ctx.params` — the validated action params
- `ctx.state` — the package state store handle

The returned string becomes the tool result.

## Open design question carried by this fixture

Whether order actions (`submit_order`, `cancel_order`) may write the strategy
book directly, or whether state writes belong to the agent via a dedicated
state tool — see #22 / #24.
