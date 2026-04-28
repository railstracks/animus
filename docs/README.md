# LLM Provider API Documentation

This directory contains API reference documentation for LLM providers being evaluated for Animus integration.

## Directory Structure

```
docs/
├── README.md              ← this file
├── ollama/api-reference.md
├── zai/api-reference.md
├── openai/api-reference.md
├── cohere/api-reference.md
└── mistral/api-reference.md
```

## Compatibility Matrix

All providers were evaluated on 2026-04-28.

| Provider | Endpoint | Auth | Protocol | Streaming | OpenAI-compatible? |
|---|---|---|---|---|---|
| **OpenAI** | `https://api.openai.com/v1` | Bearer token or OAuth | REST + SSE | Yes | *is the reference* |
| **Z.ai** | `https://api.z.ai/api/paas/v4` | Bearer token | REST + SSE | Yes | Yes — same shape, different base URL |
| **Ollama** | `http://localhost:11434/v1` | None (ignored) | REST + SSE | Yes | Yes — `/v1/chat/completions` compat layer |
| **Mistral** | `https://api.mistral.ai/v1` | Bearer token | REST + SSE | Yes | Yes — identical endpoint shape |
| **Cohere** | `https://api.cohere.com/v2/chat` | Bearer token | REST + SSE | Yes | **No** — own format, different defaults |

### What "OpenAI-compatible" means

There is no formal standard. The `/v1/chat/completions` convention is a de facto protocol:
- Request: POST with `model`, `messages` (role/content pairs), `temperature`, `max_tokens`, `stream`
- Non-streaming response: `choices[].message.content`
- Streaming response: SSE with `choices[].delta.content` chunks, terminated by `data: [DONE]`
- Auth: `Authorization: Bearer <key>`

Each provider adds their own extensions (Z.ai's `thinking`, Ollama's `num_ctx`, Mistral's FIM endpoint), but the core chat shape is shared.

### Implications for Animus architecture

- **One shared HTTP client** handles OpenAI, Z.ai, Ollama, and Mistral (ticket 014)
- **Cohere needs a separate implementation** — different request/response format, different streaming event types
- Per-provider differences (base URL, auth, extra params) handled via configuration, not code forks
- The `extra` config map in `LLMProviderConfig` handles provider-specific fields without polluting the interface

## Active Subscriptions (as of 2026-04-28)

- **OpenAI-Codex** — OAuth via ChatGPT sign-in
- **Z.ai** — API key subscription (GLM-5.1 primary model)
- **Ollama** — local, no auth needed

## Source URLs

| Provider | Documentation |
|---|---|
| Ollama | <https://ollama.readthedocs.io/en/api/> |
| Z.ai | <https://docs.z.ai/api-reference/introduction> |
| OpenAI | <https://developers.openai.com/api/reference> |
| OpenAI Codex | <https://github.com/openai/codex> |
| Cohere | <https://docs.cohere.com/reference/chat-stream> |
| Mistral | <https://docs.mistral.ai/api> |
