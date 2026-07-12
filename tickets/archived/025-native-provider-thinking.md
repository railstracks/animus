# Ticket 025 — Native Provider Thinking (Retire Custom Reasoning)

**Priority:** P0 (architectural correction, blocks clean tool calling)
**Status:** Done
**Dependencies:** Ticket 021 (Tool Calling and Reasoning Mode) — supersedes its reasoning-mode design
**Updated:** 2026-05-01

## Summary

Replace the custom "content-as-thinking, reply-tool-as-response" reasoning approach with native provider thinking support. All three target providers (Ollama, Cohere, Z.ai) expose first-class thinking/reasoning fields in their APIs. This eliminates the `reply` tool, removes model ambiguity, and aligns with how LLMs are trained to behave.

## Problem

Ticket 021 implemented reasoning mode by repurposing two channels:

- `content` → model's "private thinking space" (deliberation, hidden from user)
- `reply` tool call → user-facing response (routed through `tool_calls`)

This creates three problems:

1. **Semantic mismatch.** Tool calls are for auxiliary actions (search, execute, read) that feed results back into the next turn. The final response is not a tool call — it's the answer. Models resist this because they're trained to put their answer in `content`.

2. **Behavioral ambiguity.** The model must decide: "do I put my response in `content` or in the `reply` tool?" Different models resolve this differently:
   - Gemma: everything in the tool call, empty content
   - GLM-5.1: initially everything in content, needs explicit correction
   - Cohere/Command-A: everything in content, never makes the tool call

3. **Fighting the model's natural bias.** When `tool_calls` are present, `content` is typically null in the OpenAI API spec — this isn't just a standard, it's a behavioral model bias. Models expect to respond via `content` when they're done reasoning.

## Solution: Native Provider Thinking

All three target providers have native thinking/reasoning support:

### Ollama (native `/api/chat`)

**Request:** `"think": true` (boolean or `"high"/"medium"/"low"` for supported models)

**Response (non-streaming):**
```json
{
  "message": {
    "role": "assistant",
    "content": "The actual answer to the user",
    "thinking": "The reasoning trace — model's inner monologue",
    "tool_calls": [...]
  }
}
```

**Response (streaming):** SSE chunks interleave `message.thinking` tokens first, then `message.content` tokens. Each chunk has one or the other.

**Docs:** <https://docs.ollama.com/api/chat>, <https://docs.ollama.com/capabilities/thinking>

### Cohere (`v2/chat`)

**Request:** `"thinking": { "type": "enabled" }` (or `"disabled"`)

**Response (non-streaming):**
```json
{
  "message": {
    "role": "assistant",
    "content": [
      { "type": "thinking", "thinking": "The reasoning trace..." },
      { "type": "text", "text": "The actual answer to the user" }
    ]
  },
  "finish_reason": "COMPLETE"
}
```

**Response (streaming):** SSE events — `content-delta` for thinking tokens (when type=thinking), then `content-delta` for text tokens.

**Docs:** <https://docs.cohere.com/reference/chat-stream>, <https://docs.cohere.com/docs/reasoning>

### Z.ai (OpenAI-compatible)

**Request:** `"thinking": { "type": "enabled" }`

**Response:**
```json
{
  "choices": [{
    "message": {
      "role": "assistant",
      "content": "The actual answer to the user",
      "reasoning_content": "The reasoning trace..."
    }
  }]
}
```

**Docs:** <https://docs.z.ai/api-reference/llm/chat-completion>, <https://docs.z.ai/guides/capabilities/thinking-mode>

### Unified Architecture

After this change, the response model becomes:

| Channel | Source | Purpose |
|---------|--------|---------|
| Thinking | Ollama: `message.thinking`, Cohere: `content[type=thinking]`, Z.ai: `reasoning_content` | Reasoning trace — collapsible in UI, persisted for consolidation |
| Content | `message.content` (or Cohere: `content[type=text]`) | User-facing response — always the answer |
| Tool calls | `message.tool_calls` | Auxiliary tools only (file, shell, memory, etc.) |

Three channels, three purposes, zero ambiguity. The model does what it's trained to do.

## Changes

### 1. New type: `ThinkingMode` enum

```cpp
// include/animus_kernel/llm/LLMTypes.h

enum class ThinkingMode {
    Disabled,   // No thinking — content is the response (default, backward-compatible)
    Native      // Provider-native thinking field (Ollama think, Cohere thinking, Z.ai thinking)
};
```

### 2. OllamaProvider — add `think` parameter

**Request building:** When `ThinkingMode::Native`, add `"think": true` to the request body.

**Response parsing:**
- Non-streaming: extract `message.thinking` alongside `message.content`
- Streaming: parse `message.thinking` from SSE chunks (interleaved with `message.content`)

**Thinking callback:** Route `message.thinking` tokens through the existing `ChainThinkingCallback` — same callback that the custom approach used, but now fed from the native field.

**Content:** `message.content` is always the response. No special handling.

### 3. CohereProvider — handle thinking and typed content array

**Request building:** When `ThinkingMode::Native`, add `"thinking": { "type": "enabled" }` to the request body.

**Response parsing:**
- Non-streaming: `message.content` is an array of typed blocks. Extract `content[type=thinking].thinking` as thinking, `content[type=text].text` as response text.
- Streaming: distinguish thinking content-delta events from text content-delta events (the SSE event structure distinguishes them).

**This is also a bug fix.** The current CohereProvider assumes `content` is a string. With thinking enabled, Cohere returns `content` as an array. This mismatch likely caused the empty/missing response issues we observed.

### 4. Z.ai (future provider) — native thinking

When implemented, send `"thinking": { "type": "enabled" }` and parse `reasoning_content` from the response. OpenAI-compatible response shape.

### 5. ChainRunner — eliminate reply-tool disambiguation

**Remove:**
- `ReplyTool` registration/unregistration based on reasoning mode
- Reasoning instruction injection into system prompt ("Your content is your private thinking space...")
- Content-as-deliberation logic (`userVisibleText` extraction from reply tool)
- `textCallback` for reply tool output (replaced by content-is-always-response)

**Simplify:**
- `content` is always the user-visible response → always stream via `textCallback`
- Thinking comes from the provider's native field → always stream via `thinkingCallback`
- Tool calls are only for auxiliary tools → standard tool loop

**ThinkingConfig in request:**
```cpp
struct LLMRequest {
    // ... existing fields ...
    ThinkingMode thinkingMode{ThinkingMode::Disabled};
    // Provider reads this to inject native thinking parameter
};
```

### 6. AdminServer — wire content as response

No more `textCallback` for reply tool output. `content` tokens stream to the UI directly via `tokenCallback`. Thinking tokens stream via `thinkingCallback`. The `textCallback` added in commit `6bedee5` can be removed — it was a workaround for the reply tool not reaching the UI.

### 7. ChatView — minimal changes

The UI already handles:
- `type: "thinking"` messages (collapsible thinking block)
- `type: "token"` messages (streaming text)
- `type: "text"` messages (complete text block — added in 6bedee5)

No major UI changes needed. The SSE message types from AdminServer remain the same; only the source of the data changes (native field vs. reply tool).

### 8. PromptAssembler — remove reasoning prompt injection

The custom system prompt injection ("Your content is your private thinking space...") is eliminated. When `ThinkingMode::Native`, the provider handles thinking natively — no prompt hacking needed.

### 9. Configuration

```json
{
  "reasoning": {
    "enabled": true,
    "mode": "native"
  }
}
```

- `enabled: false` → `ThinkingMode::Disabled` — content is response, no thinking
- `enabled: true, mode: "native"` → `ThinkingMode::Native` — provider-native thinking
- The old `reasoningInstruction` field becomes unnecessary (no prompt hacking)
- Per-provider override possible: `"reasoning": { "enabled": false }` for providers that don't support native thinking

### 10. Session turn storage

```cpp
struct SessionTurn {
    // ... existing fields ...

    // Replace is_deliberation with a dedicated thinking field
    std::string thinking_content;   // Native thinking trace (persisted for consolidation)
    // is_deliberation is no longer needed — content is always the response
};
```

The consolidation layer gets the thinking trace from `thinking_content`, and the response from `content`. Clean separation.

## Migration Path

1. **Add `ThinkingMode` enum and `thinking_content` field** — backward-compatible, existing code unaffected
2. **Implement OllamaProvider native thinking** — most used provider, validates the architecture
3. **Update ChainRunner to use native thinking** — remove reply-tool logic behind the new ThinkingMode
4. **Fix CohereProvider content parsing** — handle array-of-blocks format
5. **Implement CohereProvider native thinking** — send `thinking` param, parse thinking blocks
6. **Remove ReplyTool and old reasoning logic** — clean up dead code
7. **Update Admin UI** — remove old reasoning toggle, add thinking mode selector

Steps 1-3 can ship as a working increment. Steps 4-6 are the Cohere cleanup. Step 7 is polish.

## Acceptance Criteria

- [ ] `ThinkingMode` enum defined in LLMTypes.h
- [ ] OllamaProvider sends `"think": true` when `ThinkingMode::Native`
- [ ] OllamaProvider parses `message.thinking` from response (streaming and non-streaming)
- [ ] OllamaProvider routes thinking tokens through `ChainThinkingCallback`
- [ ] OllamaProvider routes `message.content` as the user-visible response
- [ ] CohereProvider sends `"thinking": { "type": "enabled" }` when `ThinkingMode::Native`
- [ ] CohereProvider handles `content` as typed array (not string) — fix existing bug
- [ ] CohereProvider extracts thinking from `content[type=thinking]` blocks
- [ ] CohereProvider extracts response text from `content[type=text]` blocks
- [ ] ChainRunner: `content` is always user-visible response (no reply-tool disambiguation)
- [ ] ChainRunner: thinking always comes from provider native field (no content-as-deliberation)
- [ ] ReplyTool is removed from the codebase
- [ ] Old reasoning instruction injection removed from PromptAssembler
- [ ] SessionTurn has `thinking_content` field for native thinking trace
- [ ] Admin UI: thinking mode toggle (disabled / native)
- [ ] Integration test: Ollama + native thinking → verify thinking captured, content displayed, no reply tool
- [ ] Integration test: Cohere + native thinking → verify thinking captured, content displayed
- [ ] Integration test: thinking disabled → current behavior unchanged (backward compatibility)

## Design Decisions

1. **Native thinking over prompt hacking.** The provider already knows how to separate reasoning from response. We trust the provider's implementation rather than trying to override it with system prompt tricks.

2. **`reply` tool eliminated entirely.** With native thinking, there is no scenario where routing the response through a tool call is the right approach. Content is the response. Tools are for auxiliary actions.

3. **Provider-specific field names, unified architecture.** Each provider uses different field names (`thinking`, `reasoning_content`, typed content blocks), but the architecture is the same: thinking field → thinking callback, content → response callback. The provider abstraction handles the mapping.

4. **Thinking is a provider concern, not a prompt concern.** The old approach injected reasoning instructions into the system prompt to control the model's behavior. The new approach sets a provider parameter. This is more reliable because it operates at the API level, not the prompt level.

5. **Backward compatible.** `ThinkingMode::Disabled` produces the same behavior as the current non-reasoning mode. No existing functionality is broken during migration.

6. **Consolidation layer gets better data.** The `thinking_content` field contains the model's actual reasoning trace (native, high-quality), not a forced "content-as-thinking" trace that may be empty or malformed.

## Out of Scope

- **Message injection during chain execution** (interjecting user messages between tool call steps) — separate future ticket
- **Non-thinking providers** — providers without native thinking support will use `ThinkingMode::Disabled`. A future `ThinkingMode::ToolBased` could be added for a `reasoning` tool approach, but this is not needed now.
- **Thinking token budget** — Cohere supports `token_budget` for limiting thinking tokens. Can be added as a config option later.
- **Thinking level control** — Ollama supports `"think": "high"/"medium"/"low"` for some models. Can be exposed in config later.
