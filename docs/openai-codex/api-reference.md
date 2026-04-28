# OpenAI-Codex Provider API Reference

**Provider ID:** `openai-codex`
**Auth type:** OAuth 2.0 (ChatGPT subscription, not API key)
**Protocol:** OpenAI-compatible (`/v1/chat/completions`)

## Overview

The `openai-codex` provider uses a ChatGPT subscription via OAuth tokens rather than a traditional OpenAI API key. The API endpoint and request/response format are identical to the standard OpenAI provider — the only difference is authentication.

This provider is used by OpenClaw and Codex CLI. Access tokens are short-lived JWTs that must be refreshed periodically using a refresh token.

## OAuth Configuration

| Field | Value |
|-------|-------|
| Client ID | `app_EMoamEEZ73f0CkXaXp7hrann` |
| Authorize endpoint | `https://auth.openai.com/oauth/authorize` |
| Token endpoint | `https://auth.openai.com/oauth/token` |
| Redirect URI | `http://localhost:1455/auth/callback` |
| Scopes | `openid profile email offline_access` |
| PKCE | Yes (S256) — for initial login only |

## Auth Token Format

Stored in `config/auth.json` (or `~/.codex/auth.json`):

```json
{
  "type": "oauth",
  "access": "eyJhbGciOiJSUzI1NiIs...",
  "refresh": "rt_...",
  "expires": 1772886215568
}
```

- `access` — RS256 JWT. Audience: `https://api.openai.com/v1`. Contains `chatgpt_account_id`, `chatgpt_plan_type` (e.g. "plus"), `chatgpt_user_id`.
- `refresh` — Refresh token with rotation (each refresh returns a new one).
- `expires` — Epoch milliseconds when the access token expires.

## Token Refresh

```
POST https://auth.openai.com/oauth/token
Content-Type: application/x-www-form-urlencoded

grant_type=refresh_token
&refresh_token={refresh_token}
&client_id=app_EMoamEEZ73f0CkXaXp7hrann
```

**Response (200):**
```json
{
  "access_token": "eyJhbGciOiJSUzI1NiIs...",
  "refresh_token": "rt_...",
  "token_type": "Bearer",
  "expires_in": 86400
}
```

**Refresh rules:**
- Refresh 5 minutes (300s) before expiry
- Each refresh returns a new `refresh_token` — must persist the updated token
- If refresh fails (token revoked, account changed), require re-login

## Chat Completions

Identical to standard OpenAI. Uses `Authorization: Bearer {access_token}` header.

```
POST https://api.openai.com/v1/chat/completions
Authorization: Bearer eyJhbGciOiJSUzI1NiIs...
Content-Type: application/json

{
  "model": "gpt-4o",
  "messages": [{"role": "user", "content": "Hello"}],
  "stream": true
}
```

SSE streaming format is identical to OpenAI (see `docs/openai/api-reference.md`).

## Animus Implementation Notes

### OpenAICodexProvider vs OpenAIProvider

The `OpenAICodexProvider` can subclass `LLMProviderBase` identically to `OpenAIProvider` — same endpoint, same request/response format. The difference:

1. **Auth:** Reads `config/auth.json` instead of `providers.json` for credentials
2. **Token refresh:** Before each request, checks if `expires - now < 300s` and refreshes if needed
3. **Token persistence:** After successful refresh, writes updated tokens back to `config/auth.json`
4. **Headers:** Same `Authorization: Bearer {access}` but the value is a JWT, not an API key

### Suggested config entry in `providers.json`:

```json
{
  "openai-codex": {
    "auth_file": "config/auth.json",
    "base_url": "https://api.openai.com/v1",
    "default_model": "gpt-4o"
  }
}
```

The `auth_file` field tells this provider where to find the OAuth credentials. Other providers use `api_key` directly.

### Files

- `include/animus_kernel/llm/OpenAICodexProvider.h`
- `src/kernel/llm/OpenAICodexProvider.cpp`

### Token lifecycle

```
Start → load auth.json → check expiry
  → if expired: refresh → save auth.json → use new access token
  → if valid: use cached access token
Request → if 401: refresh → save auth.json → retry once
```

## References

- Codex auth docs: https://developers.openai.com/codex/auth
- OpenAI API reference: https://developers.openai.com/api/reference
- Codex CI/CD auth guide: https://developers.openai.com/codex/auth/ci-cd-auth
