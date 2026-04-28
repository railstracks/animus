# Z.ai API Reference

Source: <https://docs.z.ai/api-reference/introduction>
Fetched: 2026-04-28

## API Endpoint

**General endpoint:**
```
https://api.z.ai/api/paas/v4
```

**GLM Coding Plan endpoint** (for supported tools only):
```
https://api.z.ai/api/coding/paas/v4
```

## Authentication

HTTP Bearer token:
```
Authorization: Bearer YOUR_API_KEY
```

API keys managed at <https://z.ai/manage-apikey/apikey-list>

## Chat Completion

```
POST /paas/v4/chat/completions
```

### Request Body

```json
{
  "model": "glm-5.1",
  "messages": [
    {"role": "system", "content": "You are a helpful AI assistant."},
    {"role": "user", "content": "Hello, please introduce yourself."}
  ],
  "temperature": 1.0,
  "stream": true
}
```

**Required fields:**
- `model` (string): model code — `glm-5.1`, `glm-5`, `glm-5-turbo`, `glm-5v-turbo`, etc.
- `messages` (array): message objects with `role` and `content`

**Optional fields:**
- `temperature` (float): default 1.0
- `max_tokens` (int): max output tokens
- `stream` (bool): enable SSE streaming
- `thinking` (object): `{"type": "enabled"}` for thinking/reasoning mode
- `tools` (array): function definitions for tool calling
- `tool_choice` (string): `"auto"`, `"none"`, or specific tool name

### Response (Non-streaming)

```json
{
  "id": "...",
  "choices": [{
    "index": 0,
    "message": {"role": "assistant", "content": "..."},
    "finish_reason": "stop"
  }],
  "usage": {"prompt_tokens": 10, "completion_tokens": 20, "total_tokens": 30}
}
```

### Streaming

Standard SSE format: `data: {...}\n\n` lines, terminated by `data: [DONE]`

### Special Features

**Thinking mode:**
```json
{
  "model": "glm-5.1",
  "messages": [...],
  "thinking": {"type": "enabled"},
  "stream": true
}
```

**Vision (multimodal):**
```json
{
  "model": "glm-5v-turbo",
  "messages": [{
    "role": "user",
    "content": [
      {"type": "image_url", "image_url": {"url": "https://..."}},
      {"type": "text", "text": "What are the pics about?"}
    ]
  }]
}
```
Supports `image_url`, `video_url`, and `file_url` content types.

**Function calling:**
```json
{
  "model": "glm-5.1",
  "messages": [...],
  "tools": [{
    "type": "function",
    "function": {
      "name": "get_weather",
      "description": "Get weather for a city",
      "parameters": {
        "type": "object",
        "properties": {"city": {"type": "string"}},
        "required": ["city"]
      }
    }
  }],
  "tool_choice": "auto"
}
```

### SDK Support

- **Official Python SDK:** `pip install zai-sdk`
- **Official Java SDK:** `ai.z.openapi:zai-sdk:0.3.3`
- **OpenAI Python SDK:** set `base_url="https://api.z.ai/api/paas/v4/"`
- **OpenAI Node SDK:** set `baseURL: "https://api.z.ai/api/paas/v4/"`
- **OpenAI Java SDK:** set `.baseUrl("https://api.z.ai/api/paas/v4/")`

### Key Differences from OpenAI

- Different base URL path (`/api/paas/v4` vs `/v1`)
- `thinking` parameter for reasoning mode (unique to Z.ai)
- `Accept-Language` header for response language
- Multimodal includes video and file URLs (not just images)
- Model names: `glm-5.1`, `glm-5-turbo`, etc.
