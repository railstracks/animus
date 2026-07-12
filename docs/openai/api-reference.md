# OpenAI API Reference

Source: <https://developers.openai.com/api/reference>
Fetched: 2026-04-28

## Authentication

**API Key:**
```
Authorization: Bearer sk-...
```

**OAuth (Codex/ChatGPT sign-in):**
Browser-based OAuth flow → access token cached at `~/.codex/auth.json`.
Tokens auto-refresh during active sessions.

API keys managed at <https://platform.openai.com/api-keys>

## Chat Completions

```
POST https://api.openai.com/v1/chat/completions
```

### Request Body

```json
{
  "model": "gpt-4o-mini",
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "Hello!"}
  ],
  "temperature": 1.0,
  "max_tokens": -1,
  "stream": false
}
```

**Required fields:**
- `model` (string): `gpt-4o`, `gpt-4o-mini`, `o3`, `o4-mini`, etc.
- `messages` (array): `{role, content}` objects

**Optional fields:**
- `temperature` (float): 0–2, default 1
- `max_tokens` (int): max output tokens (-1 = auto)
- `stream` (bool): SSE streaming
- `tools` / `tool_choice`: function calling
- `response_format`: structured output (JSON mode)
- `seed` (int): deterministic sampling (best-effort)
- `frequency_penalty`, `presence_penalty` (float): 0–2
- `top_p` (float): nucleus sampling
- `stop` (array): stop sequences

### Response (Non-streaming)

```json
{
  "id": "chatcmpl-...",
  "object": "chat.completion",
  "created": 1702256327,
  "model": "gpt-4o-mini",
  "choices": [{
    "index": 0,
    "message": {"role": "assistant", "content": "..."},
    "finish_reason": "stop"
  }],
  "usage": {
    "prompt_tokens": 10,
    "completion_tokens": 20,
    "total_tokens": 30
  }
}
```

### Streaming (SSE)

```
data: {"id":"...","choices":[{"delta":{"content":"token"},"index":0}]}
data: {"id":"...","choices":[{"delta":{"content":"more"},"index":0}]}
data: {"id":"...","choices":[{"delta":{},"finish_reason":"stop","index":0}]}
data: [DONE]
```

Each chunk has `choices[].delta` instead of `choices[].message`.
`delta.content` carries the token text. Final chunk has `finish_reason`.

## Responses API (newer)

```
POST https://api.openai.com/v1/responses
```

Simpler interface: accepts `input` (string or message array) instead of full `messages`.
Used by Codex and newer integrations.

## OpenAI Codex CLI

Source: <https://github.com/openai/codex>

Terminal-based coding agent. Two auth paths:

1. **Sign in with ChatGPT** — uses subscription (Plus/Pro/Business/Enterprise)
   - Browser OAuth flow → access token
   - Follows ChatGPT workspace permissions and retention settings

2. **API key** — usage-based billing at standard API rates
   - Set via `OPENAI_API_KEY` env var or `~/.codex/auth.json`
   - Recommended for CI/CD and programmatic use

Login cached at `~/.codex/auth.json` or OS credential store.
Tokens auto-refresh during active sessions.

### Codex-specific notes

- Codex cloud requires ChatGPT sign-in
- CLI and IDE extension support both methods
- Fast mode uses ChatGPT credits (API key users get standard API pricing)
- MFA required for Codex cloud access

## Rate Limits

Tier-based: Free → Tier 1 → Tier 2 → ... → Tier 5
Each tier has RPM (requests/min), TPM (tokens/min), TPD (tokens/day) limits.
HTTP 429 response with `Retry-After` header on limit exceeded.

## Key Differences (vs other providers)

- Most feature-rich API surface (Responses API, conversations, threads)
- Tier-based rate limiting (not subscription-based like Z.ai)
- OAuth path in addition to API keys
- `finish_reason` values: `stop`, `length`, `tool_calls`, `content_filter`
