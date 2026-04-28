# Ticket 015 — Chain Execution Pipeline

**Priority:** P0 (blocks 005, makes compaction functional)
**Status:** Open
**Dependencies:** Ticket 013, Ticket 014
**Updated:** 2026-04-28 — reflects five-provider architecture with LLMProviderBase

## Summary

Wire the full inference path: incoming event → session resolution → prompt assembly → LLM call → response → turn storage. This is the "thinking" loop that turns a session with turns into an actual agent response.

## Scope

### Prompt assembly

```cpp
// In animus_kernel/PromptAssembler.h

class PromptAssembler {
public:
    // Build the message list for an LLM request from session state
    LLMRequest BuildRequest(
        const Session& session,
        const std::string& userMessage,
        const std::string& systemPrompt,
        const LLMProviderConfig& providerConfig) const;

private:
    // Estimate tokens to decide if compaction should run first
    std::size_t EstimateTokens(const std::vector<SessionTurn>& turns) const;
};
```

Assembly order:
1. System prompt (from agent config, includes constitution/identity)
2. Compaction summary (if present) as a system message
3. Recent turns from session history (role → content)
4. Current user message

If estimated tokens exceed a configurable threshold (default: 80% of provider context window), trigger compaction *before* assembly.

### Chain runner

```cpp
// In animus_kernel/ChainRunner.h

struct ChainResult {
    std::string response;
    bool success{false};
    std::string error;
    int prompt_tokens{0};
    int completion_tokens{0};
    double elapsed_ms{0};
};

class ChainRunner {
public:
    ChainRunner(
        LLMProviderRegistry& providers,
        SessionManager& sessions);

    // Execute a full chain: resolve session → assemble prompt → call LLM → store turns
    ChainResult Execute(
        const IncomingEvent& event,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& model);

    // Streaming variant — calls tokenCallback as tokens arrive,
    // then stores the complete response as a turn
    ChainResult ExecuteStreaming(
        const IncomingEvent& event,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& model,
        LLMTokenCallback tokenCallback);

private:
    LLMProviderRegistry& m_providers;
    SessionManager& m_sessions;
    PromptAssembler m_assembler;
};
```

### Execution flow

```
IncomingEvent
    │
    ▼
SessionManager.Resolve(event)     → SessionContextSet (primary + context)
    │
    ▼
PromptAssembler.BuildRequest()    → LLMRequest (system + history + user message)
    │
    ▼ (optional: check token budget, trigger compaction)
    │
ILLMProvider.StreamComplete()     → token callbacks → full response
    │
    ▼
Session.AddTurn(user turn)
Session.AddTurn(assistant turn)    → stored in session with timestamps
    │
    ▼
ChainResult returned to caller
```

### Kernel integration

- Add `ChainRunner` to `AgentKernel` (constructed with references to registry + sessions)
- Expose `ChainRunner& Chains()` accessor
- System prompt configured via `KernelConfig::defaultSystemPrompt` (or per-agent in future)
- Provider selection per request (not per kernel) enables routing different agents to different models

### Files to create

- `include/animus_kernel/PromptAssembler.h`
- `include/animus_kernel/ChainRunner.h`
- `src/kernel/chain/PromptAssembler.cpp`
- `src/kernel/chain/ChainRunner.cpp`
- `tests/ChainRunnerTests.cpp` (mock provider + in-memory store)

## Acceptance Criteria

- [ ] End-to-end: user message → session resolution → prompt assembly → LLM call → response stored as turn
- [ ] System prompt injected as first message
- [ ] Compaction summary included when present
- [ ] Token budget check triggers compaction before exceeding context window
- [ ] Both user and assistant turns stored with timestamps
- [ ] Streaming path calls token callback, then stores complete response
- [ ] ChainResult includes token usage stats
- [ ] Unit test with mock provider verifies full round-trip without network
- [ ] Error handling: provider unavailable, empty response, timeout — all produce ChainResult with error

## Notes

- This is what makes `animusd` go from "session manager that stores turns" to "agent that thinks"
- The `ConversationCompactor::BuildSummaryTurn` placeholder can be upgraded to use the LLM provider once this pipeline exists — that's a natural follow-up
- Provider selection per request (not per kernel) enables routing different agents to different models
- Future: tool calling loop (LLM requests tool → execute → feed result back → continue)
