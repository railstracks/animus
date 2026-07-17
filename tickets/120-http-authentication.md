# Ticket 120: HTTP Authentication & Rate Limiting

**Status:** Scoped  
**Date:** 2026-07-17  
**Author:** Melvin + Kestrel  
**Component:** Admin Server (Drogon), Admin UI  

## Problem

The Animus admin API is currently unauthenticated. Any client that can reach the daemon's HTTP port has full access — read/write agents, sessions, channels, config. This is fine for local development but is a blocker for web-exposed deployments.

## Proposal

Two-tier authentication at the HTTP layer, enforced by a Drogon request filter that runs before all admin API routes.

### Tier 1: Static Auth Token

- **Config:** `ANIMUS_AUTH_TOKEN` env var (or config file value)
- **Usage:** Client sends `Authorization: Bearer <token>` header
- **Scope:** Full admin access. Used for Docker deployments, CI/CD, and bootstrapping user accounts.
- **If not set:** Auth is optional (backward-compatible for local dev). When unset, a startup warning is logged.
- **Comparison:** Constant-time string comparison to prevent timing attacks.

### Tier 2: User Accounts

- **Login:** `POST /api/auth/login` with `{username, password}` → returns `{token, expires_at}`
- **Token storage:** `auth_tokens` table in DB (token hash, user_id, expires_at, created_at)
- **Usage:** Client sends `Authorization: Bearer <session_token>` header
- **Logout:** `POST /api/auth/logout` → invalidates token
- **Password hashing:** bcrypt or argon2
- **Bootstrap:** First user account created via setup endpoint, authenticated by static token. Prevents chicken-and-egg.

### Auth Filter Logic

```
For every HTTP request to /api/*:

1. If auth_token not configured AND no users exist → allow (setup mode)
2. Check IP rate limit → if blocked, return 429
3. Extract Authorization header
4. If Bearer token matches static auth_token → allow
5. If Bearer token matches a valid session token in DB → allow
6. Otherwise → increment failure counter for IP, return 401
```

### Rate Limiting

- **Trigger:** 5 failed auth attempts from same IP within 60 seconds
- **Action:** Block that IP for 60 seconds (return 429 on all requests)
- **Storage:** In-memory map: `IP → {failures: [{ts}], block_until: ts}`
- **Cleanup:** Entries expire after 5 minutes of inactivity
- **Scope:** Per-process (Animus is single-process). Resets on restart — acceptable for brute-force prevention.
- **Response:** 429 Too Many Requests with `Retry-After` header

### Endpoints

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| POST | `/api/auth/login` | None | Login with username/password, returns session token |
| POST | `/api/auth/logout` | Required | Invalidate current session token |
| GET | `/api/auth/me` | Required | Return current user info |
| POST | `/api/auth/setup` | Static token | Create first user account (bootstrap) |
| GET | `/api/auth/status` | None | Returns whether auth is required and whether users exist |

### WebSocket Authentication

The admin UI's WebSocket connection needs auth too. Browsers cannot set custom headers on WebSocket upgrade requests, so:

- **Option A:** Pass token as query parameter: `WS /api/ws?token=<token>`. Simple but token appears in server logs.
- **Option B:** Use `Sec-WebSocket-Protocol` subprotocol header. Client sends `Sec-WebSocket-Protocol: bearer.<token>`. Cleaner, no URL leakage.
- **Recommendation:** Option B (subprotocol). Drogon supports reading custom headers on WS upgrade.

### Admin UI Changes

- **Login page:** Shown when auth is required and no valid session token is stored.
- **Token storage:** `localStorage` (survives page refresh). Cleared on logout.
- **Auto-redirect:** If API returns 401, redirect to login page.
- **Setup wizard:** If no users exist, prompt to create admin account (authenticated by static token).

### Config Keys

```json
{
  "auth": {
    "require_auth": true,
    "static_token": "",
    "session_ttl_hours": 168,
    "rate_limit_max_failures": 5,
    "rate_limit_window_seconds": 60,
    "rate_limit_block_seconds": 60
  }
}
```

- `require_auth` — if true, all API requests must be authenticated. Default: true when a static token or users exist.
- `static_token` — the static auth token. Primarily from `ANIMUS_AUTH_TOKEN` env var.
- `session_ttl_hours` — session token lifetime. Default: 168 (7 days).

## Implementation

### C++ — New Files

- `src/kernel/auth/AuthFilter.h/.cpp` — Drogon request filter
- `src/kernel/auth/AuthManager.h/.cpp` — Token validation, rate limiting, user management
- `src/kernel/auth/PasswordUtil.h/.cpp` — bcrypt/argon2 hashing

### C++ — Modified Files

- `src/kernel/admin/AdminServer.cpp` — Register auth filter on all routes, add auth endpoints
- `src/kernel/data/PgDataStore.cpp` — `auth_tokens` and `users` table creation + queries
- `include/animus_kernel/KernelConfig.h` — Auth config struct

### Database Schema

```sql
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    role TEXT DEFAULT 'admin',
    created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS auth_tokens (
    token_hash TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id),
    expires_at BIGINT NOT NULL,
    created_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_auth_tokens_user ON auth_tokens(user_id);
CREATE INDEX IF NOT EXISTS idx_auth_tokens_expires ON auth_tokens(expires_at);
```

### Admin UI

- `src/views/LoginView.vue` — login form + setup wizard
- `src/stores/auth.ts` — Pinia store for auth state (token storage, login/logout, auto-redirect)
- Router guard: redirect to `/login` if not authenticated
- API client: inject `Authorization: Bearer <token>` on all requests

## Acceptance Criteria

- [ ] `ANIMUS_AUTH_TOKEN` env var enables static token auth
- [ ] Unauthenticated requests return 401 when auth is required
- [ ] User accounts can be created (bootstrap via static token)
- [ ] Login endpoint returns session token
- [ ] Session token accepted for API + WebSocket auth
- [ ] 5 failed attempts from same IP in 60s → 60s block (429)
- [ ] Admin UI shows login page when auth required
- [ ] Admin UI setup flow for first user account
- [ ] Existing Docker deployments continue working (backward compatible)
- [ ] Startup warning when auth is not configured

## Dependencies

- None (foundational security feature)

## Notes

- bcrypt is available in the `crypt` library on Linux. argon2 would need a dependency (`libargon2`).
- The rate limiter is in-memory and per-process. For multi-instance deployments (not currently supported), a shared rate limiter (Redis) would be needed. Out of scope.
- The auth filter should not apply to the WebSocket upgrade for `/api/ws` — that's handled separately in the WS handler with subprotocol auth.
- Token hashing: store SHA-256 hash of session tokens in DB (not plaintext). Compare by hashing incoming token.
