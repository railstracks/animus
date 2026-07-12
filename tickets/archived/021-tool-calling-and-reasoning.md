# Ticket 021 — Tool Calling and Reasoning Mode

**Updated:** 2026-05-01
**Status:** ✅ Complete

**Completed:**
- [x] ToolTypes.h — ToolDefinition (with ToolResultMode), ToolCall, ToolResult, ToolParameter
- [x] IToolHandler interface — GetDefinition() + GetResultMode()
- [x] ToolRegistry — registration, lookup, enumeration
- [x] ReplyTool — stream_to_user result mode, conditionally registered
- [x] LLMToolDef, LLMToolCall, LLMResponse in LLMTypes.h
- [x] OpenAICompat — BuildOpenAIRequestBody includes tools[] and tool_choice
- [x] SSEToolCallAccumulator — assembles tool call delta fragments across streaming chunks
- [x] OpenAICompat — ParseToolCalls(), ExtractFinishReason()
- [x] LLMProviderBase — GetLastToolCalls(), GetLastFinishReason(); streaming accumulates via SSEToolCallAccumulator
- [x] OpenAIProvider — ParseSSELine captures finish_reason, tool calls finalized by accumulator
- [x] ChainRunner — full tool calling loop with budget enforcement
- [x] ChainRunner — reasoning mode (conditional prompt injection, reply tool)
- [x] ChainRunner — ToolResultMode routing (stream_to_user, deliver_to_model, both)
- [x] KernelConfig — reasoningEnabled, reasoningInstruction
- [x] AgentKernel — owns ToolRegistry, RegisterBuiltinTools()
- [x] SessionTurn — tool_calls, tool_call_id, tool_name, is_deliberation
- [x] PromptAssembler — skips deliberation turns, carries tool metadata
- [x] Build passes, binary links

**Remaining:**
- [x] Admin API/UI for reasoning toggle
- [ ] Integration tests (mock provider, tool calling, reasoning mode)
- [x] Non-streaming tool call support (LLMProviderBase::Complete parses tool calls)
- [x] LoadInterfacesFromDisk called on startup (was missing — interfaces API returned empty)
- [ ] Tool call budget exhaustion test
- [ ] Test isolation (session store and memory DB path hardcoded to state/)

**Completed (this session):**
- [x] Reasoning mode API toggle: PATCH/GET `/api/v1/agent` with `{reasoning: {enabled, instruction}}`
- [x] ChainRunner::UpdateReasoningMode() — propagates reasoning config changes at runtime
- [x] ToolRegistry::Unregister() — removes tools dynamically (for disabling reasoning)
- [x] Admin UI reasoning toggle — VSwitch in ChatView context panel
- [x] Non-streaming tool call parsing — `LLMProviderBase::Complete()` now calls `ParseToolCallsFromResponse()` virtual method
- [x] `ParseToolCallsFromResponse` moved to LLMProviderBase virtual with default no-op, OpenAIProvider overrides
- [x] Integration tests for reasoning toggle (tests 190-200: GET default, PATCH enable, PATCH disable)
- [x] WS conversation counter seeded from existing sessions (fix for session collision bug)
- [x] Session resolution logging in AdminServer and ChainRunner
- [x] i18n for reasoning toggle (en + nl)

## Summary

Extend the chain execution pipeline with two tightly coupled capabilities:

1. **Tool calling** — the LLM can invoke named tools (memory, execution, messaging, etc.) during a chain, with results fed back into the loop. This is the foundational capability that makes the agent an *actor*, not just a responder.

2. **Reasoning mode** — a session-level toggle that restructures the LLM's output into two channels: `content` (deliberation, hidden from user, persisted for consolidation) and a `reply` tool call (user-facing response). When reasoning mode is off, content flows directly to the user as before.

These are co-designed because reasoning mode *is* a tool calling configuration — `reply` is simply the first registered tool, and the mode toggle controls whether it (and its companion prompt instruction) are injected into the LLM request.

## Motivation

The current pipeline goes: PromptAssembler → LLM → text → UI. This is ~30% of what ChainRunner was designed to do. The architecture already anticipates `maxChainSteps=20` and `maxToolCallsPerChain=10`, but there's no tool call parsing or execution loop yet.

Without tool calling, the agent cannot:
- Store or recall memories during a conversation
- Execute commands or scripts
- Switch context to different nodes
- Send messages on specific channels

Without reasoning mode, we cannot:
- Capture an inner monologue for consolidation (the Background Introspection Layer has no traces to analyze)
- Make agent behavior inspectable at the decision level
- Distinguish strategic deliberation from user-facing response

## Architecture

### Two channels, one pipeline

The LLM response is parsed into two channels:

| Channel | Reasoning ON | Reasoning OFF |
|---------|-------------|---------------|
| `content` (text) | Deliberation — persisted, not shown to user | User-facing reply (current behavior) |
| `tool_calls` | Standard tool pipeline — `reply` is tool #1 | Standard tool pipeline — no `reply` tool available |

The model never checks a flag. The system prompt *is* the mode:

- **Reasoning ON**: prompt includes *"Your content is your private thinking space. Use the `reply` tool call to send your response to the user."* and the `reply` tool definition is injected into the tools list.
- **Reasoning OFF**: no reasoning instruction, no `reply` tool. Content goes directly to the user.

This makes the contract impossible to violate — the model literally cannot use `reply` if it's not in the tools list, and it will use content for thinking if instructed to.

### Types

```cpp
// include/animus_kernel/tools/ToolTypes.h

/// Definition of a tool the LLM can invoke.
struct ToolParameter {
    std::string name;
    std::string type;         // "string", "integer", "boolean", etc.
    std::string description;
    bool required{false};
};

struct ToolDefinition {
    std::string name;         // e.g. "reply", "memory_recall", "shell_exec"
    std::string description;
    std::vector<ToolParameter> parameters;
    ToolResultMode resultMode{ToolResultMode::deliver_to_model}; // default: feed back to model
};

/// How a tool's result should be routed.
enum class ToolResultMode {
    stream_to_user,     // Result streams to user immediately (reply, notify)
    deliver_to_model,   // Result feeds back into the LLM loop (memory_recall, file_read, shell_exec)
    both                // Result streams to user AND feeds back to model (long-running commands user watches)
};

This is a property of the tool, not the LLM's streaming behavior. The model always receives complete tool results for `deliver_to_model` and `both` tools in the next loop iteration. The question is whether the user *also* sees it in real-time.

/// A single tool call from an LLM response.
struct ToolCall {
    std::string id;           // unique call identifier from the LLM
    std::string name;         // tool name
    std::string arguments;    // JSON string of arguments
};

/// Result of executing a tool call.
struct ToolResult {
    std::string call_id;      // matches ToolCall::id
    bool success{false};
    std::string output;        // result content (JSON string or plain text)
    std::string error;         // error message if !success
};
```

### Tool registry

```cpp
// include/animus_kernel/tools/IToolHandler.h

class IToolHandler {
public:
    virtual ~IToolHandler() = default;

    /// Return the tool definition this handler implements.
    virtual ToolDefinition GetDefinition() const = 0;

    /// Execute the tool call and return the result.
    virtual ToolResult Execute(const ToolCall& call) = 0;
};
```

```cpp
// include/animus_kernel/tools/ToolRegistry.h

class ToolRegistry {
public:
    /// Register a tool handler. Owns the handler.
    void Register(std::unique_ptr<IToolHandler> handler);

    /// Look up a handler by tool name. Returns nullptr if not found.
    IToolHandler* Find(const std::string& name) const;

    /// Get all registered tool definitions (for injection into LLM request).
    std::vector<ToolDefinition> GetAllDefinitions() const;

    /// Check if a tool is registered.
    bool Has(const std::string& name) const;

private:
    std::unordered_map<std::string, std::unique_ptr<IToolHandler>> m_handlers;
    std::vector<std::string> m_order;  // registration order for deterministic listing
};
```

### LLMRequest extension

```cpp
// Extend LLMTypes.h

struct LLMRequest {
    // ... existing fields ...

    /// Tool definitions to include in the request (OpenAI-compatible).
    /// When empty, no tools are offered and the model responds in plain text.
    std::vector<ToolDefinition> tools;

    /// Tool choice: "auto" (model decides), "none" (no tools), or specific tool name.
    std::string tool_choice{"auto"};
};
```

### LLM response parsing

```cpp
// Extend LLMTypes.h

struct LLMResponse {
    LLMMessage message;              // role + content (content = deliberation in reasoning mode)
    std::vector<ToolCall> tool_calls; // parsed tool invocations
    std::string finish_reason;       // "stop", "tool_calls", "length"
    int prompt_tokens{0};
    int completion_tokens{0};
};
```

### Reply tool handler

```cpp
// src/kernel/tools/ReplyTool.h

/// The reply tool — sends a response to the user.
/// Only registered when reasoning mode is enabled.
class ReplyTool : public IToolHandler {
public:
    ToolDefinition GetDefinition() const override {
        return ToolDefinition{
            .name = "reply",
            .description = "Send a response to the user. Use this to deliver your final answer.",
            .parameters = {
                ToolParameter{.name = "text", .type = "string",
                              .description = "The message to send to the user",
                              .required = true}
            }
        };
    }

    ToolResult Execute(const ToolCall& call) override;
    // Marks the reply as the user-facing output for this chain step.
    // The ChainRunner reads this from the execution context.
};
```

### ChainRunner integration

The chain execution loop now becomes:

```
Loop (up to maxChainSteps):
    1. PromptAssembler.BuildRequest(session, userMessage, systemPrompt, config, tools)
       - If reasoning mode ON: inject reasoning instruction into system prompt
       - If reasoning mode ON: add "reply" tool to tools list
       - Include tool definitions from ToolRegistry in LLMRequest
    2. ILLMProvider.StreamComplete(request, callback) → LLMResponse
    3. Parse response:
       a. content → deliberation (reasoning ON) or user reply (reasoning OFF)
       b. tool_calls → parse each into ToolCall structs
    4. If reasoning ON:
       - Persist content as deliberation turn (hidden from user, available for consolidation)
    5. If reasoning OFF and content is non-empty:
       - Stream content to user (current behavior)
    6. For each ToolCall:
       a. If "reply": extract text, mark as user-facing output, stream to user
       b. Else: look up handler in ToolRegistry → Execute → collect ToolResult
    7. If tool_calls were executed:
       - Append tool results as "tool" role messages to conversation
       - Continue loop (go to step 1)
    8. If no tool_calls (finish_reason == "stop"):
       - End chain
```

### Provider-specific tool call serialization

Tool calls in LLM responses come in provider-specific JSON shapes. The template method pattern already handles this:

| Provider | Tool call format | Notes |
|----------|-----------------|-------|
| OpenAI | `choices[0].message.tool_calls[]` | Standard format, `function.name` + `function.arguments` |
| Z.ai | Same as OpenAI | Uses OpenAICompat |
| Ollama | Same as OpenAI | Uses OpenAICompat |
| Mistral | Same as OpenAI | Uses OpenAICompat |
| Cohere | Different format | Custom parsing in CohereProvider |

Each provider's `ParseResponse()` and `ParseSSELine()` must be extended to extract `tool_calls[]` alongside `content`. This follows the same template method pattern used for text responses.

For the request side, `BuildRequestBody()` must be extended to include the `tools[]` array when `LLMRequest.tools` is non-empty. Again, OpenAI-compatible providers share this format; Cohere needs its own.

### Configuration

```cpp
// Extend KernelConfig::AgentRuntimeConfig

struct AgentRuntimeConfig {
    AgentModelConfig model{};
    double temperature{0.7};
    std::string systemPrompt{"You are Animus."};
    AgentBudgetConfig budget{};

    // NEW: Reasoning mode configuration
    bool reasoningEnabled{false};       // When true, content = deliberation, reply via tool call
    std::string reasoningInstruction{   // Injected into system prompt when reasoningEnabled
        "Your content field is your private thinking space — "
        "plan, deliberate, consider alternatives. "
        "Never put your final response in content. "
        "Always use the 'reply' tool call to send your response to the user."
    };
};
```

Reasoning mode is a per-agent setting (in config), overridable per-session (future: session-level toggle via API). The mode resolves at prompt assembly time — no runtime branching in the hot path.

### Turn storage extension

```cpp
// Extend SessionTurn

struct SessionTurn {
    SessionTurnId turn_id{0};
    std::string role;     // "user", "assistant", "system", "tool"
    std::string content;
    std::uint64_t unix_ms{0};
    bool is_summary{false};
    std::vector<SessionTurnId> compacted_from;

    // NEW: Tool call trace
    std::vector<ToolCall> tool_calls;      // Tool calls made in this turn
    std::string tool_call_id;              // For role="tool" turns: which call this responds to
    std::string tool_name;                 // For role="tool" turns: which tool was called

    // NEW: Reasoning trace
    bool is_deliberation{false};            // True when this turn is hidden-from-user deliberation
};
```

This gives the consolidation layer exactly what it needs: the inner monologue (`is_deliberation=true` turns) alongside the actions taken (`tool_calls`) and their results (`role="tool"` turns).

## Scope

### Phase 1: Tool Calling Infrastructure

1. **ToolTypes.h** — `ToolDefinition`, `ToolCall`, `ToolResult`, `ToolParameter` structs (new file in `include/animus_kernel/tools/`)
2. **IToolHandler** — interface for tool implementations (new file in `include/animus_kernel/tools/`)
3. **ToolRegistry** — registration, lookup, definition enumeration (new file in `include/animus_kernel/tools/`)
4. **LLMRequest/LLMResponse extension** — `tools[]`, `tool_choice`, `ToolCall` parsing
5. **Provider-side tool call parsing** — extend `ParseResponse()` and `ParseSSELine()` in each provider to extract tool calls from JSON responses; extend `BuildRequestBody()` to serialize tool definitions
6. **ChainRunner tool loop** — the execute → feed back → continue cycle
7. **SessionTurn extension** — `tool_calls`, `tool_call_id`, `tool_name`, `is_deliberation` fields
8. **ToolRegistry wired into AgentKernel** — owned by kernel, accessible to ChainRunner
9. **Unit tests** — mock provider returning tool calls, round-trip through the loop

### Phase 2: Reasoning Mode

10. **ReplyTool** — tool handler for the `reply` tool call
11. **Reasoning mode in PromptAssembler** — conditional injection of reasoning instruction and `reply` tool definition based on `reasoningEnabled`
12. **Content interpretation in ChainRunner** — when reasoning ON, content → deliberation turn (persisted, not streamed to user); when reasoning OFF, content → user reply (current behavior)
13. **KernelConfig extension** — `reasoningEnabled`, `reasoningInstruction` fields
14. **Admin API/UI** — expose reasoning toggle in agent config
15. **Integration test** — reasoning mode ON: verify content hidden, reply tool output streamed, deliberation turn stored

### Phase 3: First Concrete Tools (separate ticket, listed for context)

- `memory_store` — store an observation to the memory layer
- `memory_recall` — search/recall from memory
- `shell_exec` — execute a shell command (restricted)
- `message_send` — send a message on a specific channel/interface
- `node_switch` — change execution context to a different node

## Acceptance Criteria

### Tool Calling Infrastructure

- [ ] `ToolDefinition`, `ToolCall`, `ToolResult` types defined in ToolTypes.h
- [ ] `IToolHandler` interface with `GetDefinition()` and `Execute()`
- [ ] `ToolRegistry` supports registration, lookup by name, and enumeration of all definitions
- [ ] `LLMRequest` includes optional `tools[]` and `tool_choice` fields
- [ ] `LLMResponse` includes parsed `tool_calls[]` alongside `message.content`
- [ ] `SessionTurn` extended with `tool_calls`, `tool_call_id`, `tool_name` fields
- [ ] OpenAI-compatible providers serialize tool definitions in request and parse tool calls in response
- [ ] Cohere provider handles its own tool call format
- [ ] ChainRunner loop: LLM responds with tool calls → execute via ToolRegistry → feed results back → continue
- [ ] Chain step counting works correctly (each LLM call = one step, up to `maxChainSteps`)
- [ ] Tool call budget enforced (up to `maxToolCallsPerChain` per chain)
- [ ] Unit test: mock provider returns tool calls, ChainRunner executes mock tools, loop completes with correct results
- [ ] Unit test: tool call budget exhaustion stops the chain gracefully

### Reasoning Mode

- [ ] `ReplyTool` registered when `reasoningEnabled=true`, absent when `false`
- [ ] System prompt includes reasoning instruction when `reasoningEnabled=true`
- [ ] In reasoning mode: `content` is persisted as a deliberation turn (`is_deliberation=true`), not streamed to user
- [ ] In reasoning mode: `reply` tool call output is streamed to user as the agent's response
- [ ] In plain mode: `content` is streamed to user as before (no behavior change from current)
- [ ] In plain mode: `reply` tool is not available (not in tools list)
- [ ] KernelConfig stores `reasoningEnabled` and `reasoningInstruction` with defaults
- [ ] Integration test: reasoning mode ON → verify deliberation captured, reply delivered, no content leaked to user
- [ ] Integration test: reasoning mode OFF → verify current behavior unchanged

## Design Decisions

1. **`reply` is a tool, not a special channel.** This minimizes architecture — one tool call pipeline handles everything. The only special handling is: `reply` output goes to the user; other tool output goes back into the loop.

2. **Mode is resolved at prompt assembly time.** No runtime branching on "what mode am I in?" in the hot path. The system prompt and tool list composition make it impossible for the model to get wrong.

3. **Deliberation turns are first-class session data.** They're stored as `SessionTurn` with `is_deliberation=true`, available for the consolidation layer to analyze. This is the feedstock for the Background Introspection Layer described in the constitutional memory architecture.

4. **Tool call format follows OpenAI convention.** `function.name` + `function.arguments` as JSON. This is the de facto standard that 5 of 6 providers already use. Cohere needs custom handling, which the template method pattern already accommodates.

5. **Provider-specific tool call parsing** follows the same template method pattern as text response parsing. Each provider overrides `ParseResponse()` to extract tool calls. OpenAICompat providers share common extraction logic.

## Notes

- The `maxChainSteps` and `maxToolCallsPerChain` budgets already exist in `KernelConfig::AgentBudgetConfig`. This ticket wires them into actual enforcement.
- This ticket intentionally does *not* implement the concrete tools (memory_store, shell_exec, etc.). Those are a separate ticket. This ticket builds the pipe; the concrete tools are the water.
- Models with native reasoning (Qwen3.5, o3, etc.) have an orthogonal concern: their internal reasoning tokens are a separate capture stream. This can be handled via a `capture_native_reasoning` provider config flag in a future ticket. Our deliberation layer captures *strategy* (why the agent chose an action), not *inference* (how the model solved a problem).
- The `tool_choice` field supports "auto" (model decides), "none" (no tools), or a specific tool name. This enables forcing a tool call when needed (e.g., always reason before responding).