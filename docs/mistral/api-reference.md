# Mistral API Reference

Source: <https://docs.mistral.ai/api>
OpenAPI spec: <https://docs.mistral.ai/openapi.yaml>
Fetched: 2026-04-28

## Chat Completion

```
POST https://api.mistral.ai/v1/chat/completions
Authorization: Bearer YOUR_API_KEY
Content-Type: application/json
```

### Request Body

```json
{
  "model": "mistral-small-latest",
  "messages": [
    {"role": "user", "content": "Hello!"}
  ],
  "temperature": 0.7,
  "max_tokens": 1024,
  "stream": false
}
```

**Required fields:**
- `model` (string): `mistral-large-latest`, `mistral-small-latest`, `mistral-medium-latest`, `open-mistral-nemo`, etc.
- `messages` (array): chat message objects

**Optional fields:**
- `temperature` (float): sampling temperature
- `max_tokens` (int): max output tokens
- `stream` (bool): SSE streaming
- `top_p` (float): nucleus sampling
- `stop` (array): stop sequences
- `tools` / `tool_choice`: function calling
- `response_format`: JSON mode (`{"type": "json_object"}`)

### Response

```json
{
  "id": "cmpl-e5cc70bb28c444948073e77776eb30ef",
  "object": "chat.completion",
  "created": 1702256327,
  "model": "mistral-small-latest",
  "choices": [{
    "index": 0,
    "message": {"role": "assistant", "content": "..."},
    "finish_reason": "stop"
  }],
  "usage": {"prompt_tokens": 10, "completion_tokens": 20, "total_tokens": 30}
}
```

### Streaming

Standard SSE format (same as OpenAI): `data: {...}\n\n` terminated by `data: [DONE]`

## SDK Usage

**Python:**
```python
from mistralai import Mistral

client = Mistral(api_key="MISTRAL_API_KEY")
result = client.chat.complete(
    model="mistral-small-latest",
    messages=[{"role": "user", "content": "Hello"}],
    stream=False
)
```

**TypeScript:**
```typescript
import { Mistral } from "@mistralai/mistralai";

const mistral = new Mistral({ apiKey: "MISTRAL_API_KEY" });
const result = await mistral.chat.complete({
    model: "mistral-small-latest",
    messages: [{ role: "user", content: "Hello" }],
});
```

**cURL:**
```bash
curl https://api.mistral.ai/v1/chat/completions \
  -X POST \
  -H 'Authorization: Bearer YOUR_APIKEY_HERE' \
  -H 'Content-Type: application/json' \
  -d '{"messages": [{"content": "Hello", "role": "user"}], "model": "mistral-large-latest"}'
```

## Available Endpoints

| Endpoint | Description |
|---|---|
| `POST /v1/chat/completions` | Chat completion |
| `POST /v1/fim/completions` | Fill-in-the-middle (code completion) |
| `POST /v1/embeddings` | Generate embeddings |
| `POST /v1/classifiers` | Text classification |
| `POST /v1/files` | File management |
| `GET /v1/models` | List models |
| `POST /v1/batch` | Batch processing |
| `POST /v1/ocr` | OCR |
| `POST /v1/audio/speech` | Text-to-speech |
| `POST /v1/audio/transcriptions` | Speech-to-text |

## Beta Features

- **Agents:** autonomous agents with tool use
- **Conversations:** multi-turn conversation management
- **Libraries:** document/knowledge base management
- **Workflows:** orchestration and execution pipelines
- **Observability:** monitoring and evaluation

## Key Differences from OpenAI

- **OpenAI-compatible**: same `/v1/chat/completions` endpoint shape and SSE format
- Auth via `Authorization: Bearer` (same pattern)
- Model names use Mistral's naming: `mistral-large-latest`, `mistral-small-latest`, etc.
- FIM (fill-in-the-middle) endpoint for code completion
- OCR endpoint (not commonly found elsewhere)
- Beta agents/conversations API (more opinionated than raw chat)
- Rich observability tooling for production monitoring

## Implications for Animus

Mistral is fully OpenAI-compatible for chat completions. The same `OpenAICompatProvider` from ticket 014 would work with just a base URL and API key change. No special handling needed for basic chat + streaming.
