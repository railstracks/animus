# Ticket 013 — LLM Provider Interface and Base Class

**Priority:** P0 (blocks 014, 015, 016, 005)
**Status:** Open
**Dependencies:** None

## Summary

Define the `ILLMProvider` interface, the `LLMProviderBase` shared HTTP/streaming machinery, the provider registry, and wire both into the kernel. This ticket establishes the entire abstraction layer — no concrete provider implementations yet (those are ticket 014).

## Architecture

```
ILLMProvider (pure interface)
    │
    ▼
LLMProviderBase (HTTP + SSE + response assembly)
    │
    ├── OpenAIProvider      (standard /v1/chat/completions)
    ├── ZaiProvider         (Z.ai extensions)
    ├── OllamaProvider      (no auth, local)
    ├── MistralProvider     (near-identical to OpenAI)
    └── CohereProvider      (different protocol, overrides more)
```

The kernel only sees `ILLMProvider`. The base class handles HTTP plumbing and normalizes all streaming into a unified `LLMToken` protocol. Concrete providers are thin subclasses that know their provider's request/response quirks.

## Scope

### 1. Types and interfaces

```cpp
// include/animus_kernel/llm/LLMTypes.h

struct LLMMessage {
    std::string role;    // "system", "user", "assistant", "tool"
    std::string content;
    // Future: multimodal content parts
};

struct LLMRequest {
    std::string model;
    std::vector<LLMMessage> messages;
    float temperature{1.0f};
    int max_tokens{-1};          // -1 = provider default
    bool stream{true};
    std::unordered_map<std::string, std::string> extra; // provider-specific
};

struct LLMToken {
    std::string content;
    bool is_final{false};
    std::string finish_reason;   // "stop", "length", "tool_calls", ""
    int prompt_tokens{0};
    int completion_tokens{0};
};

using LLMTokenCallback = std::function<void(const LLMToken&)>;
```

### 2. ILLMProvider interface

```cpp
// include/animus_kernel/llm/ILLMProvider.h

class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;

    virtual LLMMessage Complete(const LLMRequest& request, std::string* error) = 0;
    virtual LLMMessage StreamComplete(
        const LLMRequest& request,
        LLMTokenCallback callback,
        std::string* error) = 0;

    virtual std::string ProviderId() const = 0;
    virtual bool IsAvailable() const = 0;
};
```

### 3. LLMProviderBase — shared HTTP/SSE machinery

```cpp
// include/animus_kernel/llm/LLMProviderBase.h

class LLMProviderBase : public ILLMProvider {
public:
    explicit LLMProviderBase(const LLMProviderConfig& config);

    // ILLMProvider — implemented here using the template methods below
    LLMMessage Complete(const LLMRequest& request, std::string* error) override;
    LLMMessage StreamComplete(
        const LLMRequest& request,
        LLMTokenCallback callback,
        std::string* error) override;
    bool IsAvailable() const override;

protected:
    // --- Template methods: concrete providers override these ---

    // Build the JSON request body for this provider
    virtual std::string BuildRequestBody(const LLMRequest& request) const = 0;

    // Parse a non-streaming JSON response into LLMMessage
    virtual LLMMessage ParseResponse(const std::string& body, std::string* error) const = 0;

    // Parse a single SSE data line into a token (if available)
    // Returns std::nullopt for lines that don't carry content (e.g. keep-alive)
    virtual std::optional<LLMToken> ParseSSELine(const std::string& line) const = 0;

    // Return true if this SSE line signals end-of-stream
    virtual bool IsSSEDone(const std::string& line) const;

    // Build the full URL for the chat endpoint
    virtual std::string GetEndpointURL() const;

    // Build HTTP headers (auth, content-type, provider-specific)
    virtual std::vector<std::pair<std::string, std::string>> GetHeaders() const;

    // --- Shared machinery (owned by base class) ---

    // Execute HTTP POST, handle connection, timeouts, error codes
    // For streaming: calls tokenCallback per SSE chunk
    // For non-streaming: returns full body
    int DoHTTPRequest(
        const std::string& body,
        bool stream,
        LLMTokenCallback tokenCallback,  // empty for non-streaming
        std::string* responseBody,        // set for non-streaming
        std::string* error);

    // Configuration access for subclasses
    const LLMProviderConfig& Config() const { return m_config; }

private:
    LLMProviderConfig m_config;
    bool m_available{true};
    // curl handle, timeout config, connection state live here
};
```

### 4. Provider configuration

```cpp
// include/animus_kernel/llm/LLMProviderConfig.h

struct LLMProviderConfig {
    std::string provider_id;      // "openai", "zai", "ollama", "mistral", "cohere"
    std::string base_url;
    std::string api_key;          // empty for Ollama
    std::string default_model;
    int connect_timeout_ms{30000};
    int stream_idle_timeout_ms{120000};
    std::unordered_map<std::string, std::string> extra;
};
```

### 5. Provider registry

```cpp
// include/animus_kernel/llm/LLMProviderRegistry.h

class LLMProviderRegistry {
public:
    using Factory = std::function<std::unique_ptr<ILLMProvider>(const LLMProviderConfig&)>;

    void Register(const std::string& id, Factory factory);
    std::unique_ptr<ILLMProvider> Create(const LLMProviderConfig& config) const;
    std::vector<std::string> Available() const;

private:
    std::unordered_map<std::string, Factory> m_factories;
};
```

### 6. Kernel integration

- Add `LLMProviderRegistry` to `AgentKernel` (owned, configured at startup)
- Extend `KernelConfig` with `std::vector<LLMProviderConfig> llmProviders`
- Provider instances created during `AgentKernel::Start()`

### Files to create

- `include/animus_kernel/llm/LLMTypes.h`
- `include/animus_kernel/llm/ILLMProvider.h`
- `include/animus_kernel/llm/LLMProviderBase.h`
- `include/animus_kernel/llm/LLMProviderConfig.h`
- `include/animus_kernel/llm/LLMProviderRegistry.h`
- `src/kernel/llm/LLMProviderBase.cpp` — HTTP/SSE implementation (libcurl)
- `src/kernel/llm/LLMProviderRegistry.cpp` — registry

### Dependencies

- **libcurl** — HTTP client (already a transitive dep via drogon)
- **nlohmann/json** — JSON serialization (header-only, via FetchContent)

## Acceptance Criteria

- [ ] `ILLMProvider` interface compiles with `LLMTypes` types
- [ ] `LLMProviderBase` implements `Complete()` and `StreamComplete()` using template methods
- [ ] `DoHTTPRequest()` handles HTTP POST with configurable timeouts
- [ ] SSE parser handles `data: {...}` lines, detects `[DONE]`, yields tokens
- [ ] `LLMProviderRegistry` supports register/create/list
- [ ] `AgentKernel` owns a registry and bootstraps providers from config
- [ ] Unit test: register a mock provider (overrides template methods), verify round-trip
- [ ] Unit test: SSE parser produces correct token sequence from fixture data
- [ ] No concrete providers yet — only mock in tests

## Notes

- Token callback design enables WebSocket streaming (ticket 005) and observation hooks
- Base class handles: connection pooling, retry/backoff on 429, timeout management, request logging
- Cohere needs deeper overrides (different SSE event format) — that's fine, the template methods support it
- `extra` config map handles provider-specific params (Z.ai `thinking`, Ollama `num_ctx`) without interface pollution
