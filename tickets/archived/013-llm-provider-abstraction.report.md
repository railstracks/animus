# Ticket 013 — LLM Provider Abstraction: Implementation Report

**Branch:** `feat/llm-provider-abstraction`
**Commits:** `d048f18`, `12497c8`, `1ccdeec`, `5d4d98a`, `a447237`
**Status:** ✅ Complete — 12/12 tests passing
**Built on:** Melvin's workstation (Linux Mint 22.x, g++ 13.3, CMake 3.28, libcurl 8.5)

## What was built

A three-layer LLM provider abstraction following the template method pattern:

```
ILLMProvider (pure interface)
    └── LLMProviderBase (HTTP/SSE machinery, template methods)
            ├── OpenAIProvider          (ticket 015)
            │   └── OpenAICodexProvider  (ticket 015, OAuth refresh)
            ├── ZaiProvider              (ticket 015)
            ├── OllamaProvider           (ticket 015)
            ├── MistralProvider          (ticket 015)
            └── CohereProvider           (ticket 015)
```

### Files created

**Headers** (`include/animus_kernel/llm/`):
- `LLMTypes.h` — `LLMMessage`, `LLMRequest`, `LLMToken`, `LLMTokenCallback`
- `ILLMProvider.h` — pure interface: `Complete()`, `StreamComplete()`, `ProviderId()`, `IsAvailable()`
- `LLMProviderConfig.h` — config struct with `provider_id`, `base_url`, `api_key`, `default_model`, timeouts, `extra` map
- `LLMProviderBase.h` — base class with public SSE processing, protected template methods and HTTP machinery
- `LLMProviderRegistry.h` — factory: `Register()`, `Create()`, `Available()`, `Has()`

**Sources** (`src/kernel/llm/`):
- `LLMProviderBase.cpp` — full HTTP/SSE implementation (libcurl, incremental parsing, streaming callbacks)
- `LLMProviderRegistry.cpp` — registry implementation

**Modified kernel files:**
- `include/animus_kernel/KernelConfig.h` — added `std::vector<llm::LLMProviderConfig> llmProviders`
- `include/animus_kernel/AgentKernel.h` — owns `LLMProviderRegistry`, exposes via `LLMRegistry()`
- `src/kernel/AgentKernel.cpp` — constructs registry in constructor

**Build system:**
- `CMakeLists.txt` — added `find_package(CURL)`, new source files, test target `animus_llm_provider_tests`

**Tests** (`tests/`):
- `LLMProviderTests.cpp` — 12 unit tests with `MockProvider` (no network calls)

**Config & docs:**
- `config/providers.example.json` — committed template with all six providers
- `config/providers.json` — gitignored, created on workstation (mode 600)
- `docs/` — API reference docs for all six providers + README with compatibility matrix

## Design decisions

### Template method pattern

`LLMProviderBase` implements the full HTTP lifecycle (connect, POST, read response, handle errors) and exposes protected "template methods" for concrete providers to customize:
- `BuildRequestBody()` — provider-specific JSON format
- `ParseResponse()` — non-streaming response parsing
- `ParseSSELine()` — per-line streaming token extraction
- `IsSSEDone()` — stream termination detection
- `GetEndpointURL()` — endpoint path (default: `/chat/completions`)
- `GetHeaders()` — auth and content-type headers

This means Cohere (completely different protocol) overrides 6/6 methods, while Mistral (near-identical to OpenAI) overrides 2/6.

### Incremental SSE parsing

The streaming implementation parses SSE lines incrementally as data arrives from curl, rather than buffering the entire response. The curl write callback (`StreamWriteCallback`) feeds raw bytes into `AppendSSEData()`, which splits on newlines and calls `ProcessSSELine()` for each complete line. This means tokens are delivered to the callback in near-real-time.

### Access specifier layout

`AppendSSEData()` and `ProcessSSELine()` are public (needed by the anonymous-namespace streaming callback), while the template methods and `DoHTTPRequest()` are protected. This was a build-driven decision — the curl callback is a free function, not a member, so it can't access protected methods.

### libcurl pimpl

`HTTPImpl` (the curl handle, header list, response buffers) is a private nested struct with pimpl pattern. The header file never includes `<curl/curl.h>`, keeping curl as an implementation detail.

## Build issues encountered

1. **No libcurl dev headers** in Kestrel's container — installed `libcurl4-openssl-dev` on Melvin's workstation instead
2. **Stale Codex modifications** on workstation — `ModuleManager.h` had `InterfaceModuleInfo` not in git main; fixed by copying git-main version
3. **Protected access from callback** — first attempt had the streaming callback calling protected methods; resolved by making SSE processing methods public
4. **Forward declaration conflict** — `StreamContext` forward-declared in namespace conflicted with anonymous-namespace definition; removed forward declaration
5. **Mutagen sync timestamps** — file updates didn't trigger rebuilds; fixed with `touch`
6. **Test namespace qualification** — `llm::LLMProviderConfig` didn't resolve inside `using namespace animus::kernel::llm`; fully qualified

## Test coverage

| Test | What it verifies |
|------|-----------------|
| LLMRequest defaults | temperature=1.0, max_tokens=-1, stream=true |
| LLMToken defaults | is_final=false, finish_reason="", token counts=0 |
| LLMProviderConfig defaults | connect_timeout_ms=30000, stream_idle_timeout_ms=120000 |
| Registry register + create | factory registration, instance creation, ProviderId |
| Registry create unknown | returns nullptr for unregistered id |
| Registry available | lists all registered ids |
| MockProvider BuildRequestBody | infrastructure works (limited without server) |
| MockProvider ParseResponse | provider construction and availability |
| IsSSEDone | default [DONE] detection |
| Provider unavailable | returns error when marked unavailable |
| GetEndpointURL | default appends /chat/completions |
| KernelConfig with providers | accepts llmProviders vector |

Tests use a `MockProvider` subclass that overrides all template methods. No network calls — HTTP testing will be covered in ticket 017 (integration tests).

## What's next

- **Ticket 014** — Provider Config Admin UI (CRUD, OAuth flow, connectivity tests)
- **Ticket 015** — Six concrete provider implementations
- **Ticket 016** — Chain execution pipeline (incoming event → LLM call → response)
- **Ticket 017** — Integration tests with real provider endpoints
