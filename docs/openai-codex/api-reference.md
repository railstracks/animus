# OpenAI-Codex Provider API Reference

Last verified: 2026-05-04

**Provider ID:** `openai-codex`
**Auth type:** OAuth 2.0 token pair (ChatGPT account/subscription, not API key billing)
**Inference protocol:** Responses-style API on ChatGPT backend (`/backend-api/codex/responses`)

## Overview

`openai-codex` is distinct from the normal `openai` provider:

- `openai`: API key auth, typically `https://api.openai.com/v1/...`
- `openai-codex`: OAuth access token auth, ChatGPT backend endpoints

Current evidence indicates Codex should be integrated as a **Responses transport**, not Chat Completions.

## Confidence Legend

- **Confirmed:** Observed in live endpoint behavior and/or stable upstream implementation.
- **Inferred:** Strongly implied by upstream integration/tests but not formally documented by OpenAI public docs.

## Endpoint Map

| Purpose | Method + URL | Status |
|---|---|---|
| Device code request | `POST https://auth.openai.com/api/accounts/deviceauth/usercode` | Confirmed |
| Device code poll | `POST https://auth.openai.com/api/accounts/deviceauth/token` | Confirmed |
| OAuth token exchange/refresh | `POST https://auth.openai.com/oauth/token` | Confirmed |
| Codex inference (Responses) | `POST https://chatgpt.com/backend-api/codex/responses` | Confirmed |
| Codex model listing | `GET https://chatgpt.com/backend-api/codex/models?client_version=<value>` | Confirmed |
| Codex usage/rate window | `GET https://chatgpt.com/backend-api/wham/usage` | Confirmed |

## OAuth Details

### Client and auth host

- `client_id`: `app_EMoamEEZ73f0CkXaXp7hrann`
- OAuth host: `https://auth.openai.com`

### Device flow

1. Request device code:

```http
POST /api/accounts/deviceauth/usercode
Content-Type: application/json

{"client_id":"app_EMoamEEZ73f0CkXaXp7hrann"}
```

Example response fields:

```json
{
  "device_auth_id": "deviceauth_...",
  "user_code": "ABCD-EFGHI",
  "interval": "5",
  "expires_at": "2026-05-04T05:51:32.872717+00:00"
}
```

2. Poll authorization:

```http
POST /api/accounts/deviceauth/token
Content-Type: application/json

{"device_auth_id":"...","user_code":"..."}
```

Pending states may return errors such as `deviceauth_authorization_unknown` (HTTP 403/404 behavior seen in upstream handling).

3. Exchange for OAuth tokens:

```http
POST /oauth/token
Content-Type: application/x-www-form-urlencoded

grant_type=authorization_code
&code=<authorization_code>
&redirect_uri=https://auth.openai.com/deviceauth/callback
&client_id=app_EMoamEEZ73f0CkXaXp7hrann
&code_verifier=<code_verifier>
```

### Refresh flow

```http
POST /oauth/token
Content-Type: application/x-www-form-urlencoded

grant_type=refresh_token
&refresh_token=<refresh_token>
&client_id=app_EMoamEEZ73f0CkXaXp7hrann
```

Typical success body includes `access_token`, `refresh_token`, and `expires_in`.

## Inference API (Codex Responses)

### Base URL

- Canonical base URL: `https://chatgpt.com/backend-api/codex`
- Canonical request path: `/responses`
- Model discovery path: `/models` (requires `client_version` query parameter; e.g. `client_version=1.0.0`)

Notes:

- Upstream compatibility logic still recognizes legacy `https://chatgpt.com/backend-api` and `/v1` forms.
- Upstream test notes indicate the `/backend-api/responses` alias was removed server-side in 2026-04.

### Request shape

Codex uses Responses-style payloads:

```json
{
  "model": "gpt-5.4-pro",
  "input": [
    {
      "role": "user",
      "content": [
        {"type":"input_text","text":"Hello"}
      ]
    }
  ],
  "stream": true,
  "instructions": "optional system instructions",
  "tools": []
}
```

Header:

```http
Authorization: Bearer <oauth_access_token>
```

### Streaming events

Observed/handled event types include:

- `response.created`
- `response.output_item.added`
- `response.output_text.delta`
- `response.refusal.delta`
- `response.function_call_arguments.delta`
- `response.output_item.done`
- `response.completed`

Usage is returned on completion under `response.usage` (with input/output/total token fields and cached token details).

## Codex-Specific Compatibility Notes

For native `chatgpt.com/backend-api/codex` backend, OpenClaw sanitizes out these request fields:

- `max_output_tokens`
- `metadata`
- `prompt_cache_retention`
- `service_tier`
- `temperature`

Status: **Inferred** compatibility behavior from upstream code/tests.

Practical guidance: support field filtering/feature flags until Animus verifies backend acceptance empirically.

## Usage Endpoint

`GET https://chatgpt.com/backend-api/wham/usage` returns rate-window usage and plan/balance metadata.

- Requires Bearer token.
- Optional header: `ChatGPT-Account-Id`.
- Upstream parses `rate_limit.primary_window` and `rate_limit.secondary_window`.

## Token Storage (Animus convention)

Animus currently persists tokens under an auth JSON section like:

```json
{
  "openai-codex": {
    "type": "oauth",
    "access": "...",
    "refresh": "...",
    "expires": 1772886215568
  }
}
```

Refresh threshold currently used in Animus: 5 minutes before expiry.

## Animus Gap vs Target Protocol

Current Animus provider wiring refreshes OAuth correctly but still inherits Chat Completions transport defaults. Target behavior for Codex should be:

1. Use `https://chatgpt.com/backend-api/codex/responses` for inference.
2. Build/parse Responses payload and SSE events.
3. Keep OAuth token refresh lifecycle on `auth.openai.com/oauth/token`.

## References

- OpenClaw repo (investigated implementation and tests):  
  `https://github.com/openclaw/openclaw` (commit inspected: `8f75a4ebdf2a6cc5e99ec93c6f25b8ac040b8301`)
- OpenAI streaming reference (event model baseline):  
  https://platform.openai.com/docs/api-reference/streaming  
  https://platform.openai.com/docs/api-reference/responses-streaming/response/reasoning/delta
