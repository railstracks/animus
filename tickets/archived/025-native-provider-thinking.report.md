# Ticket 025 — Completion Report: Native Provider Thinking

**Date:** 2026-05-02
**Status:** Complete (Ollama + Cohere production; Z.ai + OpenAI-Codex pending)
**Commits:** `b3f7d28` → `41d99ad` (14 commits)
**Files changed:** 30+

---

## What We Set Out To Do

Replace the custom ReplyTool-based reasoning architecture (where the model was instructed to put deliberation in `content` and the user-facing reply in a `reply` tool call) with native provider thinking support. Three providers were targeted: Ollama, Cohere, and Z.ai — each exposing first-class thinking/reasoning fields in their APIs.

The core insight, from Melvin: *"Don't fight model training bias."* LLMs are trained to put their answer in `content`. Forcing them to route through a custom `reply` tool creates semantic confusion and unreliable behavior across models.

---

## What Happened

This ticket turned into a multi-phase effort spanning two sessions with a context break in between. What started as a provider-level change cascaded into a full-stack rearchitecture touching the data model, persistence layer, streaming pipeline, and UI.

### Phase 1: ReplyTool Removal + Provider Implementation

**Commits:** `b3f7d28`, `15b91e9`

- Deleted `ReplyTool.h` and `ReplyTool.cpp` entirely (~80 net lines removed while adding capability)
- Added `ThinkingMode` enum and `thinking_content` field to `LLMResponse` in `LLMTypes.h`
- Updated three providers with native thinking parameters:
  - **Ollama:** `think: true` → `reasoning_content` field in SSE
  - **Cohere:** `thinking: {type: "enabled"}` → typed content array `[{type: "thinking"}, {type: "text"}]`
  - **Z.ai:** `thinking: {type: "enabled"}` → `reasoning_content` + `content` fields
- Updated `ChainRunner` to remove ReplyTool registration, use native thinking flow
- Updated `KernelConfig.h` to remove ReplyTool-era "content is private" language
- Updated `LLMProviderBase` streaming to exclude thinking tokens from accumulated content

### Phase 2: Context Loss + Recovery

Between the first session and the second, context was lost. The architectural decisions from late in session 1 — replacing `is_thinking` (boolean, separate turns) with `thinking_content` (string property on same turn) — were not yet committed. The fresh session started from the committed codebase, which still had the old approach.

This led to a **wrong migration** (`2120fff`) that persisted `is_thinking` as an INTEGER column instead of `thinking_content` as TEXT. Caught and corrected in Phase 3.

**Lesson:** Uncommitted WIP across session boundaries is indistinguishable from non-existent work.

### Phase 3: Architectural Pivot — `is_thinking` → `thinking_content`

**Commit:** `3461cf5`

The key design decision: thinking should not be a separate `SessionTurn` with a boolean flag. It should be a `std::string thinking_content` property on the *same* assistant turn as the response.

**Why this matters:**
- One session turn = one assistant response, with optional thinking trace attached
- `is_thinking` as a boolean created a separate turn, meaning thinking and response were disconnected siblings
- `thinking_content` as a string property means the DB has one row with both columns — thinking and content are always paired
- `PromptAssembler` skips empty-content turns (thinking content is never sent to the LLM), but the turn itself participates in the conversation structure

**Files changed (7):**
- `Session.h` — `bool is_thinking` → `std::string thinking_content`; added `MutableTurns()`
- `Session.cpp` — implemented `MutableTurns()`
- `ChainRunner.cpp` — `ProcessResponse` stores `thinking_content` on the assistant turn, no separate thinking turn
- `ChainRunner.h` — updated `ProcessResponse` signature
- `PromptAssembler.cpp` — skip empty-content turns instead of `is_thinking`-flagged turns
- `AdminServer.cpp` — `BuildSessionTurnJson` outputs `thinking_content`; removed `accumulatedText`
- `SqliteSessionStore.cpp` — column is `thinking_content TEXT` with safe migration via `PRAGMA table_info`

### Phase 4: Cohere SSE Parsing Fix + Double Message Fix

**Commit:** `e1b51e8`

Two bugs found during testing:

**Bug 1 — Thinking content silently dropped from Cohere:**
Cohere's v2 reasoning API sends thinking via `content-delta` events with `delta.message.content.thinking` — not a separate event type. Our SSE parser was checking for `"thinking_delta"` as a type indicator, which doesn't exist in Cohere's actual streaming format. Every thinking token was silently dropped.

Fix: detect thinking by finding `"thinking"` as a JSON key after `"content"` in the SSE line. For non-streaming, extract `type: "thinking"` entries into `msg.thinking_content`.

**Bug 2 — Double message in streaming:**
`ProcessResponse` set `userVisibleText = content` for content-only responses. The streaming path sent this via `textCallback`. But content was already streamed token-by-token via `tokenCallback`. Result: the UI received every token during streaming, then the complete text again as a `type: "text"` WebSocket event.

Fix: added `toolOutputText` output parameter to `ProcessResponse`. `textCallback` only fires for tool results, not for main LLM content.

### Phase 5: UI Unification

**Commits:** `0d6d526`, `f636bf6`, `0dab070`, `1dc15ab`

Four rounds of UI fixes, converging on the correct architecture:

**Round 1 (`0d6d526`):** History loading was setting `isThinking: true` when `thinking_content` was present, causing the entire turn to render as a thinking block with the response text inside it. Fixed by always rendering both independently.

**Round 2 (`f636bf6`):** Unified thinking and content into a single `UiMessage` for both streaming and reload. Previously, streaming created two separate UI elements (via `streamingThinkingIdBySession` tracking map) while reload showed one merged element. Now both paths use the same logic. Removed `streamingThinkingIdBySession` and `isThinking` entirely.

**Round 3 (`0dab070`):** Moved the thinking section outside the `.bubble` div as a sibling container. Both containers are conditionally rendered — thinking-only turns show just the thinking block, content-only turns show just the message.

**Round 4 (`1dc15ab`):** Added `.message-content-group` wrapper with `flex-direction: column` to stack thinking and message vertically inside the aligned row. Fixed side-by-side layout caused by the flex-row on `.message-row`.

### Phase 6: `ThinkingMode` Enum → `reasoning_effort` String

**Commits:** `6a23af2`, `3dbd3f8`, `41d99ad`

Replaced the boolean `ThinkingMode::Native` enum with a string-based `reasoning_effort` field supporting `"none"/"low"/"medium"/"high"/"xhigh"`.

**Why:** Ollama's `/v1/chat/completions` endpoint accepts `reasoning_effort` as an enum, not a boolean `"think":true`. OpenAI also supports `"xhigh"`. The boolean abstraction couldn't express this.

**Provider mappings:**
- **Ollama:** sends `"reasoning_effort":"high"` directly (was `"think":true`)
  - Non-streaming: extracts `"reasoning"` key (was ignored)
  - SSE: checks `"reasoning_content"` then falls back to `"reasoning"`
- **Z.ai/Cohere:** map any non-empty effort to `"thinking":{"type":"enabled"}` (no effort levels)
- **OpenAI (future):** would send `"reasoning_effort":"high"` or `"xhigh"` directly

**Other changes:**
- `LLMMessage` now has `thinking_content` (was only on `LLMResponse`)
- `CallLLM` copies `thinking_content` from `LLMMessage` → `LLMResponse`
- `ChainRunner` stores effort level, passes to requests
- `KernelConfig` has `reasoning_effort` field (default `"high"`)

**UI effort selector (`3dbd3f8`):** Added a `v-select` dropdown below the reasoning toggle that appears when reasoning is enabled. Options: `low`, `medium`, `high`. Changing it immediately PATCHes the agent config. Backend persists `reasoning_effort` alongside the enabled boolean.

---

## Final Architecture

### Data model

```
SessionTurn {
    role: "assistant"
    content: "The user-facing response"
    thinking_content: "The reasoning trace (optional)"
    tool_calls: [...]
    tool_call_id / tool_name: (for role="tool" turns)
}
```

One row in SQLite per turn. `thinking_content` is a TEXT column. `tool_calls` serialized as JSON.

### Streaming pipeline

```
LLM SSE → Provider.ParseSSELine()
  → thinking tokens: is_thinking=true → ChainRunner stepTokenCallback
    → accumulatedThinking += token.content
    → m_thinkingCallback(token.content) → AdminServer → WS type:"thinking" → Vue
      → message.thinkingContent += content
  → content tokens: is_thinking=false → ChainRunner stepTokenCallback
    → tokenCallback(token) → AdminServer → WS type:"token" → Vue
      → message.content += content
  → final: ProcessResponse(accumulatedThinking)
    → SessionTurn with content + thinking_content → SQLite
```

### Reasoning effort flow

```
UI toggle + select → PATCH /api/v1/agent {reasoning: {enabled, effort}}
  → AdminServer → AgentKernel → ChainRunner.SetReasoningEffort(effort)
  → On next request: assembly.request.reasoning_effort = m_reasoningEffort
  → Provider.BuildRequestBody() maps effort to native parameter
```

### UI rendering

```
<article class="message-row">              ← flex row (horizontal alignment)
  <div class="message-content-group">      ← flex column (vertical stacking)
    <div class="thinking-section">         ← v-if="thinkingContent" (collapsible)
    <div class="bubble">                   ← v-if="content" (standard message)
  </div>
</article>
```

---

## Commits

| Hash | Description |
|------|-------------|
| `b3f7d28` | ticket(025): native provider thinking - retire custom reasoning approach |
| `15b91e9` | feat(t025): native provider thinking - replace ReplyTool reasoning |
| `4cfed5e` | chore: rebuild UI assets + update provider configs |
| `2120fff` | feat(persistence): persist thinking + tool_call columns in SQLite |
| `415af0d` | up |
| `3461cf5` | feat(t025): replace is_thinking with thinking_content on SessionTurn |
| `e1b51e8` | fix(cohere): capture thinking content from SSE + fix double message |
| `0d6d526` | fix(ui): show thinking block above message on reload, not instead of it |
| `f636bf6` | fix(ui): unify thinking+content into single UiMessage for streaming |
| `0dab070` | fix(ui): separate thinking and content into independent containers |
| `1dc15ab` | fix(ui): stack thinking and message vertically via wrapper div |
| `6a23af2` | feat(t025): replace ThinkingMode enum with reasoning_effort string |
| `3dbd3f8` | feat(ui): add reasoning effort selector (low/medium/high) |
| `41d99ad` | chore: rebuild binary with embedded effort select UI |

---

## Provider Status

| Provider | Thinking | Effort Levels | Status |
|----------|----------|---------------|--------|
| **Ollama** | ✅ `"reasoning_effort"` param, `"reasoning"` key in response | low/medium/high | **Production** |
| **Cohere** | ✅ `"thinking":{"type":"enabled"}`, SSE content-delta | Boolean only (effort ignored) | **Production** |
| **Z.ai** | ✅ `"thinking":{"type":"enabled"}`, `"reasoning_content"` in SSE | Boolean only (effort ignored) | **Blocked** — HTTP 429 rate limit |
| **OpenAI-Codex** | ⬜ `reasoning_effort` param, `reasoning_content` key | low/medium/high/xhigh | **Blocked** — OAuth not yet wired |

---

## Lessons Learned

1. **Uncommitted WIP is invisible.** The architectural decision to replace `is_thinking` with `thinking_content` was made in session 1 but not committed. Session 2 had no record of it and re-implemented the old approach. Commit early, especially across session boundaries.

2. **Read the actual API docs, not assumptions.** The Cohere SSE parser was built on an assumed format (`thinking_delta` type) that didn't match Cohere's actual v2 streaming spec. The real format sends thinking and text through the same `content-delta` event type, distinguished by field name (`thinking` vs `text`), not by event type.

3. **Separate concerns in the callback pipeline.** Mixing `userVisibleText` (LLM content) with tool output in the same `textCallback` caused the double-message bug. The fix was a separate `toolOutputText` output parameter — `textCallback` fires only for tool results, not for content that was already streamed via `tokenCallback`.

4. **UI state tracking should mirror the data model.** When the data model has one turn with two properties (`content` + `thinking_content`), the UI should have one `UiMessage` with two properties — not two separate tracked elements. The `streamingThinkingIdBySession` map was an artifact of treating thinking as a separate message, which contradicted the data model.

5. **Booleans become enums.** The `ThinkingMode::Native` enum was essentially a boolean (only two values). When Ollama turned out to accept effort levels, the boolean couldn't express it. Strings with validation helpers are more resilient than enums for cross-provider parameters.
