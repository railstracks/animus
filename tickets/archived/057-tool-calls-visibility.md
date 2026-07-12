# Ticket 057 — Tool Calls Visibility in Admin Chat UI

**Status:** Implemented (Phases 1-3)  
**Priority:** Medium  
**Created:** 2026-05-23  
**Depends on:** None  
**Blocks:** None

## Problem

The admin chat UI shows tool *results* (collapsible green blocks) but never shows tool *calls* — what the agent decided to call, with what arguments, before the result came back. This makes it hard to understand agent behavior: you see the output of a tool but not the reasoning or parameters that produced it.

Additionally, there is a structural mismatch between **live streaming** and **history loading**:

### Streaming behavior (current)

During a multi-step chain (agent → tool call → tool result → agent continues), the frontend creates **one** streaming assistant message (`findOrCreateStreamingAssistantMessage`) and appends all text tokens to it regardless of chain step. When a tool result arrives, it is pushed as a separate `tool` UiMessage. The visual result:

```
[assistant] "Let me check thatHere is what I found"   ← concatenated
[tool]      email result
```

Both text segments ("Let me check that" and "Here is what I found") are in the same bubble. The tool result appears *between* them during streaming, but the assistant text before and after the tool call is concatenated into a single string.

### History behavior (current)

`loadSessionHistory` creates one `UiMessage` per `SessionTurn`. The backend stores each chain step as a separate assistant turn. So history renders:

```
[assistant] "Let me check that"   (turn has tool_calls)
[tool]      email result
[assistant] "Here is what I found"
```

This means **the same conversation looks different depending on whether you watched it live or loaded it from history**. Neither rendering shows the tool *calls* themselves.

## Root Cause

1. **No `tool_call` WebSocket event.** The backend only emits `tool_result` events (via `toolEventCallback` in `ChatSessionService.cpp`). It never sends a `tool_call` event before execution begins. The frontend has no way to show "Calling email..." in real time.

2. **Streaming concatenates across chain steps.** `findOrCreateStreamingAssistantMessage()` reuses the same message ID across the entire chain, so all text from all steps merges into one bubble.

3. **History doesn't render `tool_calls`.** `SessionTurn.tool_calls` is populated in the backend and returned by the history API, but `loadSessionHistory` maps turns to `UiMessage` without using the `tool_calls` field. Only `tool_name` and `tool_call_id` are used (for `tool` role turns), not the `tool_calls` array on `assistant` role turns.

4. **No visual pairing.** Even if tool calls were rendered, there's no visual connection between a call and its result.

## Implementation Plan

### Phase 1: History Rendering (no backend changes)

When loading session history, if an assistant turn has `tool_calls`, render each call as a compact collapsible block *before* the corresponding result turn.

**Changes:**

- `SessionTurn` type already has `tool_calls?: Array<{id?, name?, arguments?}>` — no type change needed.
- `loadSessionHistory()`: After building UiMessages from turns, insert a new `tool_call` UiMessage before each `tool` role message. The `tool_call` message shows the tool name + formatted arguments in a collapsible block.
- Add `tool_calls` to `UiMessage` type (or reuse `toolName`/`toolCallId` fields from a new role like `'tool_call'`).

Visual design for tool call blocks:
- Compact, collapsed by default (just tool name shown, e.g. "🔧 email → list_inboxes")
- Expandable to show formatted JSON arguments
- Color: amber/orange tint (distinct from green tool results)
- Paired visually with the result below it (shared left border or connecting line)

### Phase 2: Live Streaming

Add a `tool_call` WebSocket event that fires *before* tool execution begins, so the UI can show "Calling email..." immediately.

**Backend changes:**

In `ChatSessionService.cpp`, the `toolEventCallback` currently only fires *after* tool execution (it receives both `ToolCall` and `ToolResult`). Split this: emit a `tool_call` event before execution, and keep `tool_result` after.

In `ChainRunner::ProcessResponse`, before executing each tool:
```cpp
// Existing: toolEventCallback(tc, toolResult);  // after execution
// Add: emit tool_call event before execution
```

This requires either:
- A new callback parameter (pre-execution callback), or
- Extending `ChainToolEventCallback` to a richer event type that includes phase (before/after)

Simplest approach: add a `ChainToolCallCallback` (called before execution) alongside the existing `ChainToolEventCallback` (called after). Wire both in `ChatSessionService`.

**WebSocket events:**
```json
{ "type": "tool_call", "session_id": "42", "tool_call_id": "call_abc", "tool_name": "email", "arguments": "{\"action\":\"list_inboxes\"}" }
{ "type": "tool_result", "session_id": "42", "tool_call_id": "call_abc", "tool_name": "email", "success": true, "content": "..." }
```

**Frontend changes:**
- Handle `tool_call` WebSocket message type in `handleSocketMessage`
- Push a `tool_call` UiMessage when the event arrives
- Show a loading spinner or "Calling..." indicator on the tool call block
- When the corresponding `tool_result` arrives, update the tool call block to show completion and render the result below it

### Phase 3: Chain Step Boundary Visibility

The more fundamental issue: multi-step chains should render as distinct steps, not concatenated text.

**Option A:** Create a new assistant message per chain step during streaming. When a `tool_call` or `tool_result` event arrives, finalize the current streaming assistant message and start a new one for the next text segment.

**Option B:** Keep concatenation but add a visual separator (e.g., a thin divider line) where a tool call interrupted the text flow.

Option A is cleaner and matches history rendering. It requires the backend to signal "end of this text segment" — which the `tool_call` event (Phase 2) naturally provides. When the frontend receives a `tool_call` event, it finalizes the current streaming message and starts fresh after the result.

**Recommendation:** Phase 1 first (quick win, no backend changes), then Phase 2 (live tool call events), then Phase 3 (step boundaries, using Phase 2's events to split the streaming message).

## Files Changed

### Phase 1 (frontend only)
- `admin-ui/src/views/ChatView.vue` — add tool_call rendering in `loadSessionHistory`, add `UiMessage` role handling for `tool_call`, add CSS for call blocks

### Phase 2 (full stack)
- `src/kernel/chain/ChainRunner.cpp` — emit pre-execution tool_call event (new callback or split existing)
- `include/animus_kernel/ChainRunner.h` — add `ChainToolCallCallback` type or extend existing callback
- `src/kernel/admin/ChatSessionService.cpp` — wire new callback, emit `tool_call` WebSocket event
- `admin-ui/src/views/ChatView.vue` — handle `tool_call` WebSocket event type

### Phase 3 (frontend)
- `admin-ui/src/views/ChatView.vue` — finalize streaming message on `tool_call` event, start new message after `tool_result`

## Open Questions

1. **Call block color:** Amber/orange for calls, green for results? Or a different palette? (Recommendation: amber for calls, teal for results — matches existing tool-section styling but warmer for "outgoing" vs "incoming".)

2. **Arguments formatting:** Pretty-print JSON arguments? Truncate long arguments with "show more"? (Recommendation: pretty-print, collapsible, max 10 lines visible by default.)

3. **Phase 3 step boundaries:** Should each chain step get its own assistant avatar/timestamp, or should the whole chain be one logical "response" with internal step dividers? (Recommendation: one logical response, with subtle step dividers. This matches how other AI chat UIs handle multi-step reasoning.)

4. **Should tool call IDs be displayed?** The `tool_call_id` is an opaque string like `call_abc123`. Not useful to users but helpful for debugging. (Recommendation: show in a tooltip on hover, not in the main view.)
