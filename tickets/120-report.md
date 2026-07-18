# Ticket 120: HTTP Authentication & Rate Limiting — Implementation Report

**Ticket:** 120
**Status:** Implemented
**Date:** 2026-07-17 → 2026-07-18
**Author:** Kestrel
**Branch:** `dev`
**Commits:** `8e228de` through `3008d45` (12 commits, including build fixes)

## Summary

Full HTTP authentication system for the Animus admin API. Two-tier auth: static token via `ANIMUS_AUTH_TOKEN` env var (zero-config Docker security) and user accounts with session tokens (interactive use). Rate limiting, role-based access control, and complete admin UI integration.

## Implementation

### Backend (C++)

**AuthStore** (`src/kernel/auth/AuthStore.cpp`, `include/animus_kernel/AuthStore.h`)
- Two DB tables: `auth_users` (id, username, password_hash, role, created_at) and `auth_tokens` (token_hash, user_id, expires_at, created_at)
- Indices on token user_id and expires_at for cleanup queries
- Full CRUD: CreateUser, GetUserByUsername, GetUserById, ListUsers, UpdateUserPassword, UpdateUserRole, DeleteUser, CreateToken, GetToken, DeleteToken, DeleteTokensForUser, ListTokensForUser, CleanExpiredTokens, UserCount, HasUsers

**AuthManager** (`src/kernel/auth/AuthManager.cpp`, `include/animus_kernel/AuthManager.h`)
- SHA-256 password hashing with random salt (stored as `salt:hash`)
- Session token generation: 32 random bytes → 64 hex chars. Raw token SHA-256 hashed before DB storage.
- Token validation: static token (constant-time compare) → session token (hash lookup + expiry check)
- In-memory rate limiting: per-IP failure tracking, 50 failures/60s window → 60s block
- 7-day session TTL

**AuthHelpers** (`include/animus_kernel/AuthHelpers.h`)
- `ExtractBearerToken()` — parses Authorization header
- `ExtractClientIp()` — checks X-Forwarded-For, falls back to peer address
- `CheckAuth()` — inline helper for routes that need user ID

**Route Protection — Drogon Sync Advice** (`src/kernel/admin/AdminServer.cpp`)
- `drogon::app().registerSyncAdvice()` intercepts every `/api/` request before routing
- Public exemptions: `/api/v1/status`, `/api/v1/auth/status`, `/api/v1/auth/login`, `/api/v1/auth/setup`, `/api/v1/system/first-run`
- Returns 401 for missing/invalid tokens, 429 when rate limited

**Role-Based Access Control**
- User management (`/api/v1/users/*`): admin-only for all methods
- Channel management (`/api/v1/channels/*`): admin-only for writes (POST/PUT/PATCH/DELETE), reads (GET) available to any authenticated user
- Provider management (`/api/v1/providers/*`): same as channels — writes admin-only, reads open
- Static token auth (no userId): treated as full admin, no restriction

**API Endpoints** (`src/kernel/admin/internal/AdminServerRoutesAuth.inc`)
- `GET /api/v1/auth/status` — auth required? users exist? static token? (public)
- `POST /api/v1/auth/login` — username/password → session token (public, rate-limited)
- `POST /api/v1/auth/logout` — revoke current token
- `GET /api/v1/auth/me` — current auth method + user info
- `POST /api/v1/auth/setup` — create first admin account (requires static token)
- `GET /api/v1/users` — list users (admin)
- `POST /api/v1/users` — create user (admin)
- `PUT /api/v1/users/:id` — update password/role (admin)
- `DELETE /api/v1/users/:id` — delete user (admin)
- `GET /api/v1/users/:id/sessions` — list active tokens (admin)
- `DELETE /api/v1/users/:id/sessions` — revoke all tokens for user (admin)

**Kernel Integration** (`src/kernel/AgentKernel.cpp`)
- `ANIMUS_AUTH_TOKEN` env var read at startup via `std::getenv()`
- `ConfigureAuth()` called before `AdminServer::Start()`
- Startup log indicates auth mode: static token, user accounts, or warning if unconfigured

### Admin UI (Vue)

**Auth Store** (`stores/auth.ts`)
- Plain composable (no Pinia dependency) — module-level singleton refs
- localStorage persistence under `animus_auth_token` key
- Methods: login, logout, setup, setupWithStaticToken, checkStatus, fetchMe

**LoginView** (`views/LoginView.vue`)
- Tabbed UI: Credentials tab (username/password) and Auth Token tab (static token)
- Token path: verifies via `/auth/me`, sets token, proceeds to app
- Setup prompt: if token works but no users exist, offers admin account creation
- Default tab auto-selects based on server state (token if no users, credentials otherwise)

**UsersView** (`views/UsersView.vue`)
- Data table with create/edit/delete dialogs
- Role selection (admin/viewer)
- Session revocation per user
- Auth handled by global fetch interceptor (no manual headers)

**Router Guard** (`router/index.ts`)
- `beforeEach` checks `authRequired.value` and `isAuthenticated.value` (ref unwrap)
- Redirects to `/login` when auth required and not authenticated
- Public routes: `/login`, `/wizard`

**Global Fetch Interceptor** (`main.ts`)
- Injects `Authorization: Bearer <token>` from localStorage on all `/api/` requests
- Matches both relative (`/api/...`) and absolute (`http://host/api/...`) URLs
- 401 handler: clears token, redirects to `/login` (except on auth endpoints)

**Sidebar** (`components/AppSidebar.vue`)
- Hides Channels, Providers, Users nav items for non-admin roles
- Shows account section (username, sign out) when auth is required
- Login/wizard pages render without sidebar

### i18n
- "users" sidebar label added to en + 22 locale files

## Build Issues Encountered and Fixed

1. **`@/stores/auth` import** — project has no `@` path alias. Changed to relative imports.
2. **Pinia dependency** — project doesn't use Pinia. Rewrote auth store as plain composable.
3. **`IStatement::Exec()`** — no such method. `Exec()` is on `IDataStore`; `IStatement` uses `Step()`.
4. **Namespace** — `ConfigureAuth` defined outside `animus::kernel` namespace.
5. **Discord gateway duplicate declarations** — old `EventLoop` and `wsPtr` left behind during reconnect refactor.
6. **Vue ref unwrap** — router guard and LoginView compared Ref objects (always truthy) instead of `.value`.
7. **Fetch interceptor URL matching** — `startsWith('/api/')` didn't match absolute URLs from `api.ts`. Changed to `includes('/api/')`.
8. **Dual localStorage keys** — `api.ts` used `animus_admin_token`, auth store used `animus_auth_token`. Unified.
9. **UsersView Ref unwrap** — `const token = auth.token` grabbed Ref object, serialized as `[object Object]`.

## Auth Flow

1. **No `ANIMUS_AUTH_TOKEN`, no users** → auth optional (backward-compatible, warning logged)
2. **`ANIMUS_AUTH_TOKEN` set, no users** → static token grants full access. `/setup` endpoint creates first admin.
3. **Users exist** → login with username/password → session token (7-day TTL). Static token still works if set.

## Files Changed

**New files:**
- `include/animus_kernel/AuthStore.h`, `AuthManager.h`, `AuthHelpers.h`
- `src/kernel/auth/AuthStore.cpp`, `AuthManager.cpp`
- `src/kernel/admin/internal/AdminServerRoutesAuth.inc`
- `admin-ui/src/stores/auth.ts`
- `admin-ui/src/views/LoginView.vue`, `UsersView.vue`

**Modified files:**
- `include/animus_kernel/AdminServer.h` (auth members, ConfigureAuth, RegisterRoutesAuth)
- `src/kernel/admin/AdminServer.cpp` (sync advice, ConfigureAuth, RegisterHandlersOnce)
- `src/kernel/admin/AdminServerRoutes.cpp` (RegisterRoutesAuth)
- `src/kernel/AgentKernel.cpp` (ConfigureAuth call at startup)
- `CMakeLists.txt` (new source files)
- `admin-ui/src/main.ts` (fetch interceptor)
- `admin-ui/src/router/index.ts` (auth guard, LoginView/UsersView routes)
- `admin-ui/src/components/AppSidebar.vue` (Users link, sign out, role filtering)
- `admin-ui/src/App.vue` (hide nav on login page)
- `admin-ui/src/i18n/locales/*/sidebar.ts` (users label)
- `scripts/start-daemon.sh` (auth status output)
