# Cohere API Reference

Source: <https://docs.cohere.com/reference/chat-stream>
Fetched: 2026-04-28

## Chat API (v2)

```
POST https://api.cohere.com/v2/chat
Content-Type: application/json
Authorization: Bearer YOUR_API_KEY
```

**Important:** Cohere's v2 API is **NOT** OpenAI-compatible. It has its own request/response format.

### Request Body

```json
{
  "model": "command-r-plus",
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "Hello!"}
  ],
  "temperature": 0.3,
  "max_tokens": 1024,
  "stream": true
}
```

**Required fields:**
- `model` (string): `command-r-plus`, `command-r`, `command-r7b-12-2024`, etc.
- `messages` (array): chat message objects

**Optional fields:**
- `temperature` (float): default 0.3 (note: different from OpenAI's default 1.0)
- `max_tokens` (int): max output tokens
- `stream` (bool): SSE streaming
- `seed` (int): best-effort deterministic sampling
- `frequency_penalty` (float): 0.0–1.0, default 0.0
- `presence_penalty` (float): 0.0–1.0, default 0.0
- `k` (int): top-k sampling, 0–500, default 0 (disabled)
- `p` (float): top-p (nucleus) sampling
- `stop_sequences` (array): up to 5 stop strings
- `tools` (array): tool definitions
- `strict_tools` (bool): enforce strict tool output schema
- `documents` (array): RAG documents for grounded generation
- `citation_options`: citation behavior config
- `response_format`: structured output (JSON mode)
- `safety_mode`: `"CONTEXTUAL"`, `"STRICT"`, or `"OFF"`

### Streaming Response

Content-Type: `text/event-stream`

SSE events with different types (not the same shape as OpenAI's delta format).
Events include typed chunks: content-generation, citation, tool-call, etc.

### Error Codes

| Code | Meaning |
|---|---|
| 400 | Bad request (invalid JSON, missing fields) |
| 401 | Invalid API token |
| 403 | Insufficient permissions |
| 404 | Resource not found |
| 422 | Unprocessable entity |
| 429 | Rate limited |
| 498 | Deny-listed token in request/response |
| 499 | Client cancelled request |
| 500 | Internal server error |
| 501 | Feature not implemented |
| 503 | Service unavailable |
| 504 | Gateway timeout |

### Key Differences from OpenAI

- **NOT OpenAI-compatible** — different endpoint, request shape, and response format
- Different default temperature (0.3 vs 1.0)
- Built-in RAG via `documents` parameter with citation support
- `safety_mode` for content filtering (not present in OpenAI)
- `k` and `p` sampling parameters instead of `top_p`
- `stop_sequences` instead of `stop`
- Streaming events are typed (different event types, not just delta chunks)
- v2 API is relatively new — migration guide exists from v1

### Implications for Animus

Cohere would need its own `ILLMProvider` implementation — the OpenAI-compatible client won't work.
The differences are significant enough that it shouldn't share the same HTTP parsing logic:
- Different SSE event format
- Different request field names in places
- Built-in RAG/docs is a unique feature worth exposing
- Safety mode is Cohere-specific
