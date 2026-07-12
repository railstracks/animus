# Ticket 014 — Provider Configuration Admin UI

**Priority:** P1 (enables OAuth flow, unblocks non-technical users)
**Status:** Open
**Dependencies:** Ticket 001 (admin server scaffold), Ticket 009 (SPA scaffold), Ticket 013 (LLM provider abstraction)

## Summary

Add admin UI panels and API endpoints for managing LLM provider configuration. This replaces hand-editing `config/providers.json` and enables browser-based OAuth flows (e.g. OpenAI-Codex ChatGPT login).

## Motivation

- Provider configuration currently requires manually editing JSON files
- OAuth providers (openai-codex) need a browser-based PKCE flow — impossible from CLI alone
- Admin interface already scaffolded (tickets 001, 009) — this adds the provider management surface
- Testing connectivity should be one click, not a manual curl

## API Endpoints

### Provider CRUD

```
GET    /api/providers                    — list all configured providers with status
GET    /api/providers/:id                — get single provider config (API keys masked)
POST   /api/providers                    — create provider entry
PUT    /api/providers/:id                — update provider entry
DELETE /api/providers/:id                — remove provider entry
POST   /api/providers/:id/test           — test connectivity (sends a minimal request)
```

### OAuth Flow (openai-codex)

```
POST   /api/providers/openai-codex/auth           — initiate OAuth, returns redirect URL
GET    /api/providers/openai-codex/callback        — OAuth callback (captures code, exchanges for tokens)
GET    /api/providers/openai-codex/status          — token status (expiry, plan type, email)
```

The OAuth flow:
1. Frontend calls `POST /api/providers/openai-codex/auth`
2. Server generates PKCE `code_verifier` + `code_challenge`, stores in session
3. Server returns `{ "authorize_url": "https://auth.openai.com/oauth/authorize?..." }`
4. Frontend redirects browser to `authorize_url`
5. User logs in on auth.openai.com
6. auth.openai.com redirects back to `/api/providers/openai-codex/callback?code=...`
7. Server exchanges `code` for tokens via `POST https://auth.openai.com/oauth/token`
8. Server stores tokens in `config/auth.json`
9. Server redirects browser to SPA provider panel with success/failure status

## SPA Panels

### Provider List Panel

- Route: `/providers`
- Table/grid of configured providers
- Columns: Provider ID, Base URL, Default Model, Auth Type, Status
- Status indicators: ✅ available, ⚠️ untested, ❌ error (last error shown on hover)
- Actions per row: Edit, Test, Delete
- "Add Provider" button opens creation form

### Provider Edit/Create Form

- Dynamic form based on provider type
- Common fields: Provider ID, Base URL, Default Model
- Auth type selector:
  - `api_key` → text input for API key (masked)
  - `oauth` → "Sign in with ChatGPT" button (only for openai-codex)
  - `none` → no auth fields (Ollama)
- "Extra params" section: key-value pairs for provider-specific options
- "Test Connection" button with live result
- i18n labels throughout (no static text)

### OAuth Status Panel (openai-codex)

- Shows: logged-in email, plan type, token expiry countdown
- "Refresh Token" button (manual refresh)
- "Sign Out" button (clears tokens)

## Backend Implementation

### ProviderConfigService

New service class in the admin server that:
- Reads/writes `config/providers.json` and `config/auth.json`
- Masks API keys in GET responses (show last 4 chars only)
- Validates provider configs before saving
- Manages PKCE state for OAuth flows
- Performs connectivity tests (sends a minimal completion request)

### Config File Schema

`config/providers.json`:
```json
{
  "default_provider": "openai-codex",
  "providers": {
    "openai-codex": {
      "auth_file": "config/auth.json",
      "base_url": "https://api.openai.com/v1",
      "default_model": "gpt-4o"
    },
    "zai": {
      "api_key": "sk-...",
      "base_url": "https://api.z.ai/api/paas/v4",
      "default_model": "glm-5.1"
    }
  }
}
```

`config/auth.json` (OAuth tokens, gitignored):
```json
{
  "openai-codex": {
    "type": "oauth",
    "access": "eyJ...",
    "refresh": "rt_...",
    "expires": 1772886215568,
    "email": "user@example.com",
    "plan_type": "plus"
  }
}
```

### Config Loading

The host `main.cpp` (or a dedicated config loader module) reads `config/providers.json` at startup and populates `KernelConfig.llmProviders`. On config change via API, the kernel's provider registry is updated in-place (hot reload).

## Files to Create/Modify

### Backend
- `src/admin/ProviderConfigService.h` — provider config CRUD + OAuth flow
- `src/admin/ProviderConfigService.cpp`
- `src/admin/ProviderConfigController.h` — HTTP route handlers
- `src/admin/ProviderConfigController.cpp`
- Modify `src/admin/AdminServer.cpp` — register provider routes

### Frontend (SPA)
- `admin-ui/src/views/ProviderList.vue` — provider list/grid
- `admin-ui/src/views/ProviderEdit.vue` — create/edit form
- `admin-ui/src/views/ProviderOAuth.vue` — OAuth status/callback
- `admin-ui/src/composables/useProviders.ts` — API client for provider endpoints
- `admin-ui/src/locales/en/providers.json` — i18n labels
- `admin-ui/src/locales/nl/providers.json` — Dutch i18n labels

### Config
- `config/providers.json` — already gitignored
- `config/auth.json` — gitignored (separate from providers, holds secrets)
- `config/providers.example.json` — committed template (already exists)

## Acceptance Criteria

- [ ] Provider list API returns all configured providers with masked credentials
- [ ] Provider create/update/delete API persists to `config/providers.json`
- [ ] OAuth flow completes end-to-end: init → browser redirect → callback → token stored
- [ ] OAuth callback redirects back to SPA with success/failure status
- [ ] Connectivity test sends minimal request and reports success/error per provider
- [ ] SPA provider list shows all providers with live status
- [ ] SPA provider edit form handles all auth types (api_key, oauth, none)
- [ ] All UI text uses i18n labels, no static strings
- [ ] `config/auth.json` is gitignored and has mode 600
- [ ] API keys masked in all GET responses (last 4 chars visible)

## Notes

- The OAuth PKCE flow needs a session store (even minimal) to hold `code_verifier` between init and callback. Can use an in-memory map with TTL initially.
- The admin server's OAuth callback URL (`http://localhost:PORT/api/providers/openai-codex/callback`) must be registered as a redirect URI. For OpenAI-Codex this is `http://localhost:1455/auth/callback` per the Codex CLI convention, but we may need to configure this per-deployment.
- Hot reload: when a provider is added/updated via API, the kernel should be able to pick it up without restart. This means the config service needs a way to notify the kernel (callback, event, or poll).
- This ticket can be implemented in parallel with ticket 015 (concrete providers) since the config loading is independent of the provider implementations.
