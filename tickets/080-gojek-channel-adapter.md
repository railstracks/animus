# Ticket 080 — Gojek (GoBiz) Channel Adapter

**Priority:** P3
**Dependencies:** Public webhook relay infrastructure (shared with Teams)
**Created:** 2026-06-09

## Summary

Channel adapter for Gojek's GoBiz merchant platform API. Enables agents to manage GoFood orders, menus, outlets, payments, and promotions on behalf of merchants. REST + OAuth2 + webhooks. Indonesia + Vietnam only.

## API Overview

**Base URL:** `https://api.gobiz.co.id/`
**Auth:** OAuth2 — client_credentials (Direct) or authorization_code (Facilitator)
**Token lifetime:** 3600s, refresh_token supported
**Real-time:** Webhooks only (no WebSocket/streaming)
**Docs:** `https://developer.gobiz.com/`, `https://app.gobiz.com/files/static/cpp/docs/index.html`

### Integration Models

| Model | Auth Flow | Use Case |
|-------|-----------|----------|
| Direct Integration | client_credentials | Merchant accessing own data |
| Facilitator | authorization_code | POS/aggregator acting on behalf of merchants |

### Available Scopes

| Scope | Product | Description |
|-------|---------|-------------|
| `partner:outlet:read/write` | Outlet Management | List and manage merchant outlets |
| `gofood:catalog:read/write` | GoFood Menu | Read and modify food menus |
| `gofood:order:read/write` | GoFood Orders | Receive and manage food orders |
| `promo:food_promo:read/write` | Promotions | Manage GoFood promotions |
| `payment:transaction:read/write` | Payments | Payment transactions (QRIS) |
| `payment:pop:read` | GoPay PoP | Payment point-of-purchase data |
| `mokapos:*` | Moka POS | POS library, transactions, reporting, customers |

### Real-time: Webhooks

GoFood order notifications delivered via webhook subscriptions. Partner registers a URL, Gojek POSTs order events. **No WebSocket option.** Rate limiting discourages heavy polling.

## Proposed Adapter Actions

### Orders (GoFood)
- `list_orders` — GET /gofood/v1/orders
- `get_order` — GET /gofood/v1/orders/{id}
- `accept_order` — PUT /gofood/v1/orders/{id}/accept
- `reject_order` — PUT /gofood/v1/orders/{id}/reject
- `mark_ready` — PUT /gofood/v1/orders/{id}/ready
- `cancel_order` — PUT /gofood/v1/orders/{id}/cancel

### Menu (GoFood Catalog)
- `get_menu` — GET /gofood/v1/menu
- `update_item` — PUT /gofood/v1/menu/items/{id}
- `toggle_item` — PATCH availability on/off
- `update_price` — PATCH price on item/variant

### Outlets
- `list_outlets` — GET /integrations/partner/outlets/v1
- `get_outlet` — GET /integrations/partner/outlets/{id}/v1
- `update_outlet` — PUT operating hours, go-live toggle

### Payments
- `list_transactions` — GET payment transactions
- `get_transaction` — GET transaction details
- `create_qris` — POST QRIS payment

### Promotions
- `list_promos` — GET active promotions
- `create_promo` — POST new promotion
- `update_promo` — PUT update promotion

## Architecture

**Lua adapter** (like Moltbook/Bluesky) — pure REST, no persistent connection needed.

### Auth Handling
OAuth2 client_credentials flow — store client_id + client_secret in AgentConfigStore, token refresh handled by adapter (check expiry, re-request when stale). Animus OAuthManager may be reusable here.

### Webhook Relay (Blocker)
Real-time order notifications require a **publicly accessible endpoint**. Same blocker as Teams. Two paths:
1. **Polling fallback** — periodic REST calls for order status (degraded experience, rate-limited)
2. **Hosted relay service** — we provide a public endpoint that forwards webhooks to Animus instances. Premium value-add.

## Blockers

- **Webhook relay infrastructure** — shared requirement with Teams ticket. Without it, integration is polling-only (menu management, outlet updates, payment reconciliation). Real-time order flow requires webhook delivery.
- **GoBiz merchant account** — need sandbox credentials for development (available via GoBiz Developer Portal)
- **Region limited** — Indonesia + Vietnam only. Broader rollout depends on Gojek expansion.

## Notes

- GoPay has a **separate API** (`doc.gopay.com`) with its own auth — could be a future extension
- Moka POS integration (`mokapos:*`) adds POS-level features (inventory, customers, reporting) — out of scope for v1
- Facilitator model enables multi-merchant management (one agent serving multiple outlets) — interesting for commercial use
- Melvin + Thomas identified the webhook relay as a premium hosting opportunity — casual users won't self-host, but a hosted callback service differentiates the platform
