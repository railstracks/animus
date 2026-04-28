# Ticket 016 — Provider Configuration and Integration Test

**Priority:** P1
**Status:** Open
**Dependencies:** Ticket 014, Ticket 015
**Updated:** 2026-04-28 — five providers with active subscriptions

## Summary

End-to-end integration test and configuration wiring that proves the full stack works with real providers. Configuration file format for declaring providers in the Animus config.

## Scope

### Config file format

Extend the kernel config to support provider declarations (YAML or JSON config file alongside CLI args):

```yaml
llm:
  providers:
    - id: zai
      base_url: "https://api.z.ai/api/paas/v4"
      api_key: "${ZAI_API_KEY}"
      default_model: "glm-5.1"
      extra:
        thinking: "enabled"

    - id: openai
      base_url: "https://api.openai.com/v1"
      api_key: "${OPENAI_API_KEY}"
      default_model: "gpt-4o-mini"

    - id: ollama
      base_url: "http://localhost:11434/v1"
      default_model: "llama3"
      # no api_key needed

    - id: mistral
      base_url: "https://api.mistral.ai/v1"
      api_key: "${MISTRAL_API_KEY}"
      default_model: "mistral-small-latest"

    - id: cohere
      base_url: "https://api.cohere.com/v2"
      api_key: "${COHERE_API_KEY}"
      default_model: "command-r-plus"
      extra:
        safety_mode: "CONTEXTUAL"

  default_provider: zai
  default_system_prompt: "You are an Animus agent."
```

### Config loading

```cpp
struct LLMConfig {
    std::vector<LLMProviderConfig> providers;
    std::string defaultProvider;
    std::string defaultSystemPrompt;
};
```

- Parse from config file (YAML via yaml-cpp, or JSON via nlohmann/json)
- Environment variable interpolation for secrets (`${VAR}` → `getenv("VAR")`)
- Merge with CLI overrides if needed

### Integration tests

Each test is guarded by an env var (skipped if key not present):

1. **Z.ai smoke test** — `ANIMUS_TEST_ZAI_KEY` set
   - Send "Say hello in one word." to glm-5.1
   - Verify non-empty response, finish_reason="stop"

2. **Ollama smoke test** — `ANIMUS_TEST_OLLAMA=1`
   - Send "Say hello in one word." to default model
   - Verify non-empty response

3. **OpenAI smoke test** — `ANIMUS_TEST_OPENAI_KEY` set
   - Send "Say hello in one word." to gpt-4o-mini
   - Verify non-empty response

4. **Mistral smoke test** — `ANIMUS_TEST_MISTRAL_KEY` set
   - Send "Say hello in one word." to mistral-small-latest
   - Verify non-empty response

5. **Cohere smoke test** — `ANIMUS_TEST_COHERE_KEY` set
   - Send "Say hello in one word." to command-r-plus
   - Verify non-empty response (validates the different protocol path)

6. **Streaming test** (any available provider)
   - Stream a short prompt, verify at least 2 token callbacks
   - Verify assembled response matches concatenated tokens

7. **Full chain test** (any available provider)
   - Create session, add user turn via ChainRunner
   - Verify session has both user and assistant turns
   - Send follow-up, verify context includes first exchange

### Files to create

- `src/kernel/llm/Config.cpp` — config parsing + env interpolation
- `tests/LLMIntegrationTests.cpp` — provider smoke tests (all five)
- `tests/ChainIntegrationTests.cpp` — full chain round-trip
- `config/animus.example.yaml` — example config with all five providers

## Acceptance Criteria

- [ ] YAML config file parsed and loaded into `LLMConfig`
- [ ] Environment variable interpolation works for api_key fields
- [ ] At least two provider smoke tests pass (Z.ai + Ollama as most accessible)
- [ ] Cohere smoke test passes (validates the non-OpenAI protocol path)
- [ ] Streaming test produces multiple token callbacks
- [ ] Full chain test stores both turns and returns non-empty response
- [ ] Tests skip gracefully when provider credentials unavailable
- [ ] Example config file committed with all five providers

## Notes

- Start with Z.ai and Ollama — immediately accessible, no extra setup
- Cohere is the important validation: if the base class template method architecture works for Cohere's different protocol, the design is proven
- OpenAI OAuth may need different auth flow than static API key — can defer
- Mistral is essentially free to test once the OpenAI path works
