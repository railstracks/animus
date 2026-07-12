# Ollama API Reference

Source: <https://ollama.readthedocs.io/en/api/>
Fetched: 2026-04-28

Ollama provides two API surfaces: a native REST API and an OpenAI-compatible layer.

---

## Native API

Base URL: `http://localhost:11434`

### Generate a Completion

```
POST /api/generate
```

**Parameters:**
- `model` (required): model name in `model:tag` format (tag defaults to `latest`)
- `prompt`: the prompt text
- `images`: base64-encoded images (for multimodal models like llava)
- `format`: `json` for JSON mode output
- `options`: model parameters from Modelfile (temperature, etc.)
- `system`: override system message
- `template`: override prompt template
- `context`: context from previous request for conversational memory
- `stream`: `false` for single response (default: streaming)
- `raw`: `true` to skip prompt formatting
- `keep_alive`: how long model stays loaded after request (default: `5m`)

**Streaming response:** sequence of JSON objects:
```json
{"model": "llama3.2", "created_at": "...", "response": "The", "done": false}
```

**Final response** includes stats:
```json
{
  "model": "llama3.2",
  "response": "",
  "done": true,
  "total_duration": 10706818083,
  "load_duration": 6338219291,
  "prompt_eval_count": 26,
  "prompt_eval_duration": 130079000,
  "eval_count": 259,
  "eval_duration": 4232710000,
  "context": [1, 2, 3]
}
```

- Durations in nanoseconds
- Token speed: `eval_count / eval_duration * 10^9`

### Generate a Chat Completion

```
POST /api/chat
```

**Parameters:**
- `model` (required): model name
- `messages`: array of `{role, content, images}` objects
- `stream`: `false` for single response
- `format`: `json` for JSON mode
- `options`: model parameters (temperature, etc.)
- `keep_alive`: model retention time
- `tools`: array of tool definitions for function calling

**Chat with history:**
```json
{
  "model": "llama3.2",
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "Hello"},
    {"role": "assistant", "content": "Hi there!"},
    {"role": "user", "content": "What is recursion?"}
  ]
}
```

**Chat with tools:**
```json
{
  "model": "llama3.2",
  "messages": [{"role": "user", "content": "What is the weather in Tokyo?"}],
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
  }]
}
```

### Model Management

| Endpoint | Method | Description |
|---|---|---|
| `/api/tags` | GET | List local models |
| `/api/show` | POST | Show model information |
| `/api/copy` | POST | Copy a model |
| `/api/delete` | DELETE | Delete a model |
| `/api/pull` | POST | Pull a model from registry |
| `/api/push` | POST | Push a model to registry |
| `/api/create` | POST | Create a model from Modelfile |
| `/api/embeddings` | POST | Generate embeddings |
| `/api/ps` | GET | List running models |

### Conventions

- **Model names:** `model:tag` format, optional namespace like `example/model`
- **Durations:** always in nanoseconds
- **Streaming:** disabled with `{"stream": false}`

---

## OpenAI Compatibility Layer

Source: <https://ollama.readthedocs.io/en/openai/>

> **Note:** Experimental and subject to breaking changes.

Base URL: `http://localhost:11434/v1/`

### Supported Endpoints

| Endpoint | Description |
|---|---|
| `POST /v1/chat/completions` | Chat completions (streaming + non-streaming) |
| `POST /v1/completions` | Text completions |
| `GET /v1/models` | List available models |
| `GET /v1/models/{model}` | Get model info |
| `POST /v1/embeddings` | Generate embeddings |

### Auth

- `api_key` field is required by OpenAI SDKs but **ignored** by Ollama
- Use any string: `api_key: 'ollama'`

### Usage (Python)

```python
from openai import OpenAI

client = OpenAI(
    base_url='http://localhost:11434/v1/',
    api_key='ollama',  # required but ignored
)

chat_completion = client.chat.completions.create(
    messages=[{'role': 'user', 'content': 'Say this is a test'}],
    model='llama3.2',
)
```

### Key Differences from OpenAI

- No authentication required
- No rate limits (local inference)
- Model names use Ollama's `model:tag` format
- Response includes Ollama-specific timing stats
- Supports vision models via `image_url` content type
- `num_ctx`, `num_predict`, and other Modelfile params via `options` or extra fields
