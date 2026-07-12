# Ticket 014 Report — Provider Configuration Admin UI

**Status:** Complete (core CRUD + SPA)
**Date:** 2026-04-29
**Implemented by:** Kestrel

## What was done

### Backend (C++ / Drogon)

**KernelConfig.h:**
- Added `ProviderConfigStorage` struct with `providersFilePath` and `authFilePath` fields
- Added `providerStorage` field to `KernelConfig`

**AdminServer.h:**
- Added `ProviderState` struct (provider_id, base_url, api_key, default_model, auth_type, status, etc.)
- Added provider state members: `m_providersByName`, `m_defaultProvider`, `m_providerMutex`, `m_providerStorage`
- Added methods: `LoadProvidersFromDisk`, `SaveProvidersToDisk`, `LoadAuthFromDisk`, `SaveAuthToDisk`
- Updated `Start()` signature to accept `ProviderConfigStorage`

**AdminServer.cpp:**
- Provider config JSON helpers: `BuildProviderJson`, `ValidateProviderConfig`, `ProviderFromJson`
- Full CRUD file I/O: load/save `config/providers.json` and `config/auth.json`
- API endpoints registered in `RegisterHandlersOnce`:
  - `GET /api/v1/providers` — list all providers with masked API keys
  - `GET /api/v1/providers/{id}` — single provider (masked)
  - `POST /api/v1/providers` — create provider (validates, persists, 201 on success)
  - `PUT /api/v1/providers/{id}` — update provider (merge + persist)
  - `DELETE /api/v1/providers/{id}` — delete provider (with rollback on save failure)
  - `POST /api/v1/providers/{id}/test` — connectivity test (config validation for now, LLM layer hook pending)
  - `PATCH /api/v1/providers/default` — set default provider
- Provider loading called during `Start()` after agent config load
- SPA fallback route `/providers` added

**AgentKernel.cpp:**
- Wired `m_config.providerStorage` through to `AdminServer::Start()`

### Frontend (Vue 3 + Vuetify)

**ProvidersView.vue:**
- Provider list table with status icons (✅/⚠️/❌), masked keys, auth type chips
- Inline actions: Test, Edit, Delete, Set Default
- Create/Edit dialog with dynamic form (auth_type controls visible fields)
- Error handling with rollback on failure

**Router:**
- Added `/providers` route → `ProvidersView`

**i18n (en + nl):**
- Full provider management strings in English and Dutch
- Sidebar link "Providers" added with `mdi-cloud-cog-outline` icon

### Build verification
- ✅ CMake configure succeeds
- ✅ Full build (animus_kernel + animusd + all tests) passes
- ✅ SPA `npm run build` succeeds (689 modules, 7.9s)
- ✅ Full build with embedded UI passes
- ✅ Pre-existing tests: ModuleLoader, Session, AdminServerDisabled, AgentConfigReload all pass
- ⚠️ Pre-existing failures unchanged: JobsTests (dependency-chain), AdminServerTests (interface list)

## Deferred items

- **OAuth PKCE flow** — The `POST /api/providers/openai-codex/auth` and callback endpoints are not yet implemented. This requires session state for code_verifier and a browser redirect flow. Marked for a follow-up since it depends on deployment-specific redirect URI configuration.
- **Live connectivity test** — The `/test` endpoint validates config completeness but doesn't send an actual LLM request. Full test requires wiring through the `LLMProviderRegistry` to create a temporary provider instance and send a minimal completion. This hook is in place (the test endpoint exists) and will be enhanced when the chain execution pipeline (ticket 016) wires the registry into the kernel.
- **Hot reload** — Provider config changes are persisted to disk but don't yet trigger runtime reconfiguration of the kernel's LLM provider. This will be implemented as part of the kernel ↔ registry integration.

## Acceptance criteria status

- [x] Provider list API returns all configured providers with masked credentials
- [x] Provider create/update/delete API persists to `config/providers.json`
- [ ] OAuth flow completes end-to-end (deferred)
- [x] Connectivity test endpoint exists and reports config validity
- [x] SPA provider list shows all providers with live status
- [x] SPA provider edit form handles all auth types (api_key, oauth, none)
- [x] All UI text uses i18n labels, no static strings (en + nl)
- [ ] `config/auth.json` is gitignored and has mode 600 (gitignore check needed)
- [x] API keys masked in all GET responses (last 4 chars visible)
