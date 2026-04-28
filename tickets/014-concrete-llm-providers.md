# Ticket 014 — Concrete LLM Provider Implementations

**Priority:** P0 (blocks 015, 016)
**Status:** Open
**Dependencies:** Ticket 013

## Summary

Implement five concrete LLM providers as subclasses of `LLMProviderBase`. Four share the OpenAI `/v1/chat/completions` protocol and only override request building and minor parsing. Cohere has its own protocol and overrides more of the base class.

## Providers

### OpenAI Provider (`OpenAIProvider`)

Reference docs: `docs/openai/api-reference.md`

- **Base URL:** `https://api.openai.com/v1`
- **Endpoint:** `/chat/completions`
- **Auth:** `Authorization: Bearer <api_key>` (API key or OAuth access token)
- **Models:** `gpt-4o`, `gpt-4o-mini`, `o3`, `o4-mini`
- **Overrides:** None needed beyond `BuildRequestBody()` and `ParseResponse()` — standard OpenAI protocol
- **Future:** Responses API (`/v1/responses`) as alternative entry point

### Z.ai Provider (`ZaiProvider`)

Reference docs: `docs/zai/api-reference.md`

- **Base URL:** `https://api.z.ai/api/paas/v4`
- **Endpoint:** `/chat/completions`
- **Auth:** `Authorization: Bearer <api_key>`
- **Models:** `glm-5.1`, `glm-5-turbo`, `glm-5v-turbo`
- **Overrides from base:**
  - `GetHeaders()` — add `Accept-Language` header
  - `BuildRequestBody()` — inject `thinking` field from `extra.thinking` config
  - Response parsing handles `thinking` content blocks in streaming chunks
- **Multimodal:** supports `image_url`, `video_url`, `file_url` content types (future)

### Ollama Provider (`OllamaProvider`)

Reference docs: `docs/ollama/api-reference.md`

- **Base URL:** `http://localhost:11434/v1`
- **Endpoint:** `/chat/completions` (OpenAI compatibility layer)
- **Auth:** None (api_key ignored)
- **Models:** Any pulled model name (`llama3.2`, `mistral`, `codellama:code`, etc.)
- **Overrides from base:**
  - `GetHeaders()` — omit `Authorization` header when api_key is empty
  - `BuildRequestBody()` — map `extra` fields to Ollama `options` object (`num_ctx`, `num_predict`, `temperature`, etc.)
- **Notes:** Ollama also has a native `/api/chat` endpoint with richer stats. The `/v1/chat/completions` compat layer is sufficient for Animus's needs.

### Mistral Provider (`MistralProvider`)

Reference docs: `docs/mistral/api-reference.md`

- **Base URL:** `https://api.mistral.ai/v1`
- **Endpoint:** `/chat/completions`
- **Auth:** `Authorization: Bearer <api_key>`
- **Models:** `mistral-large-latest`, `mistral-small-latest`, `mistral-medium-latest`, `open-mistral-nemo`
- **Overrides:** Near-identical to OpenAI. Standard `/v1/chat/completions` shape.
- **Future:** FIM endpoint (`/v1/fim/completions`) for code completion, OCR endpoint

### Cohere Provider (`CohereProvider`)

Reference docs: `docs/cohere/api-reference.md`

- **Base URL:** `https://api.cohere.com/v2`
- **Endpoint:** `/chat` (NOT `/chat/completions`)
- **Auth:** `Authorization: Bearer <api_key>`
- **Models:** `command-r-plus`, `command-r`, `command-r7b-12-2024`
- **Protocol:** NOT OpenAI-compatible. Requires deeper overrides.

**Overrides from base:**
- `BuildRequestBody()` — different JSON shape, default temperature 0.3 (not 1.0), supports `documents` for RAG, `safety_mode`, `k`/`p` sampling params
- `ParseResponse()` — different response JSON structure
- `ParseSSELine()` — different SSE event types (typed events, not OpenAI delta format)
- `IsSSEDone()` — different stream termination
- `GetEndpointURL()` — returns `/chat` instead of `/chat/completions`

**Unique features to expose via `extra` config:**
- `documents` — RAG with citation support
- `safety_mode` — `"CONTEXTUAL"`, `"STRICT"`, `"OFF"`
- `k` — top-k sampling (0–500)
- `p` — top-p sampling

## Scope

### Implementation

Each provider is a thin subclass:

```cpp
// Example: OpenAIProvider (simplest case)
class OpenAIProvider : public LLMProviderBase {
public:
    explicit OpenAIProvider(const LLMProviderConfig& config)
        : LLMProviderBase(config) {}

    std::string ProviderId() const override { return "openai"; }

protected:
    std::string BuildRequestBody(const LLMRequest& request) const override;
    LLMMessage ParseResponse(const std::string& body, std::string* error) const override;
    std::optional<LLMToken> ParseSSELine(const std::string& line) const override;
};

// Example: CohereProvider (needs deeper overrides)
class CohereProvider : public LLMProviderBase {
public:
    explicit CohereProvider(const LLMProviderConfig& config)
        : LLMProviderBase(config) {}

    std::string ProviderId() const override { return "cohere"; }

protected:
    std::string GetEndpointURL() const override;  // /chat, not /chat/completions
    std::string BuildRequestBody(const LLMRequest& request) const override;
    LLMMessage ParseResponse(const std::string& body, std::string* error) const override;
    std::optional<LLMToken> ParseSSELine(const std::string& line) const override;
    bool IsSSEDone(const std::string& line) const override;
};
```

### Provider registration

```cpp
// In LLMProviderRegistry bootstrap or kernel startup:
registry.Register("openai", [](auto& cfg) { return std::make_unique<OpenAIProvider>(cfg); });
registry.Register("zai",    [](auto& cfg) { return std::make_unique<ZaiProvider>(cfg); });
registry.Register("ollama", [](auto& cfg) { return std::make_unique<OllamaProvider>(cfg); });
registry.Register("mistral",[](auto& cfg) { return std::make_unique<MistralProvider>(cfg); });
registry.Register("cohere", [](auto& cfg) { return std::make_unique<CohereProvider>(cfg); });
```

### Files to create

- `include/animus_kernel/llm/OpenAIProvider.h`
- `include/animus_kernel/llm/ZaiProvider.h`
- `include/animus_kernel/llm/OllamaProvider.h`
- `include/animus_kernel/llm/MistralProvider.h`
- `include/animus_kernel/llm/CohereProvider.h`
- `src/kernel/llm/OpenAIProvider.cpp`
- `src/kernel/llm/ZaiProvider.cpp`
- `src/kernel/llm/OllamaProvider.cpp`
- `src/kernel/llm/MistralProvider.cpp`
- `src/kernel/llm/CohereProvider.cpp`

### Testing

Each provider gets a unit test with mock HTTP responses (fixture data, not real network):

- `tests/OpenAIProviderTests.cpp` — request building, response parsing, SSE parsing
- `tests/ZaiProviderTests.cpp` — thinking field injection, response parsing
- `tests/OllamaProviderTests.cpp` — no-auth headers, options mapping
- `tests/MistralProviderTests.cpp` — request/response round-trip (very similar to OpenAI)
- `tests/CohereProviderTests.cpp` — different request shape, different SSE events

## Acceptance Criteria

- [ ] All five providers compile and implement `ILLMProvider` via `LLMProviderBase`
- [ ] OpenAI/Z.ai/Ollama/Mistral share `ParseSSELine()` logic (extracted to shared helper if needed)
- [ ] Cohere overrides SSE parsing with its own event format
- [ ] Each provider registered in the factory and creatable from config
- [ ] Unit tests for each provider with fixture HTTP responses (no network)
- [ ] Request building produces correct JSON for each provider's expected format
- [ ] SSE parser for each provider correctly tokenizes fixture streams

## Notes

- The OpenAI/Z.ai/Ollama/Mistral providers share so much that their `BuildRequestBody()` implementations could delegate to a shared `BuildOpenAIRequestBody()` helper. Worth doing if the duplication gets noticeable.
- Cohere is the proof point that the template method architecture works — if it fits cleanly, the design is right.
- Mistral is almost identical to OpenAI. Consider whether it even needs its own class or if the config alone differentiates it. (Having it explicit is cleaner for future FIM/OCR features.)
