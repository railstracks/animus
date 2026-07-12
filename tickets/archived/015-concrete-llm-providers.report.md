# Ticket 015 Report — Concrete LLM Provider Implementations

**Status:** Mostly complete — code written, not yet wired into build
**Date:** 2026-04-29

## What was done

All six concrete providers have been implemented as headers and source files:

| Provider | Source lines | Protocol | Notes |
|----------|-------------|----------|-------|
| OpenAI | 51 lines | OpenAI compat | Base class for others |
| Z.ai | 66 lines | OpenAI compat | Thinking field injection |
| Ollama | 88 lines | OpenAI compat | No-auth, options mapping |
| Mistral | header-only | OpenAI compat | Inherits OpenAIProvider, identical protocol |
| OpenAI-Codex | 210 lines | OpenAI compat + OAuth | Token refresh lifecycle, extends OpenAI |
| Cohere | 107 lines | Custom | Different endpoint, request shape, SSE format |

### Architecture

- **OpenAICompat.h/cpp** — shared helper for the 5 OpenAI-compatible providers. Contains BuildOpenAIRequestBody(), ExtractJsonString(), and common SSE parsing utilities. Key factorization: instead of duplicating request/response logic across 5 providers, a shared compat layer handles the common case.

- **OpenAICodexProvider** extends OpenAIProvider (not LLMProviderBase directly) — same protocol, just adds OAuth token refresh with persistence to config/auth.json.

- **CohereProvider** proves the template method pattern works: it overrides GetEndpointURL(), BuildRequestBody(), ParseResponse(), ParseSSELine(), and IsSSEDone() — exactly the deep override that validates the architecture.

- **MistralProvider** is intentionally header-only — it is just an alias for OpenAI protocol with a different provider ID. Separate class reserved for future FIM/OCR extensions.

### Files created

- include/animus_kernel/llm/OpenAIProvider.h
- include/animus_kernel/llm/OpenAICompat.h
- include/animus_kernel/llm/ZaiProvider.h
- include/animus_kernel/llm/OllamaProvider.h
- include/animus_kernel/llm/MistralProvider.h
- include/animus_kernel/llm/OpenAICodexProvider.h
- include/animus_kernel/llm/CohereProvider.h
- src/kernel/llm/OpenAIProvider.cpp
- src/kernel/llm/OpenAICompat.cpp
- src/kernel/llm/ZaiProvider.cpp
- src/kernel/llm/OllamaProvider.cpp
- src/kernel/llm/OpenAICodexProvider.cpp
- src/kernel/llm/CohereProvider.cpp

## What is NOT done

- **Not wired into CMakeLists.txt** — the animus_kernel library target does not include the src/kernel/llm/*.cpp files yet
- **Not registered in factory** — LLMProviderRegistry does not have the 6 provider registrations
- **No per-provider unit tests** — ticket called for fixture-based tests per provider. The existing LLMProviderTests.cpp only tests the mock/base/registry from ticket 013
- **Not integrated into AgentKernel** — kernel startup does not instantiate or register any providers
- **MistralProvider has no .cpp** — intentional (header-only), but should be noted in CMake

## Why

The provider code was written during the Codex session on April 28. The rsync disaster (ticket 015 was the active work when the --delete flag destroyed the workspace) interrupted the build integration. Recovery on April 29 restored all source files from the Codex session log replay, but the CMake wiring and test work was not completed.

## Remaining work (quick wins)

1. Add src/kernel/llm/*.cpp to CMakeLists.txt animus_kernel target sources
2. Add registry bootstrap: 6 Register() calls in kernel startup or registry init
3. Add provider-specific test files with fixture HTTP responses
4. Verify build compiles cleanly with all providers
