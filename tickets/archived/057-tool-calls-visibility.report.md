# Ticket 057 — Tool Calls Visibility in Admin Chat UI

**Report**  
**Date:** 2026-05-23  
**Status:** Implemented (Phases 1–3)  
**Commits:**  
- `0aca154` feat(admin-ui): show tool calls in chat history (Phase 1)  
- `380a3ef` feat: tool_call WebSocket event for live streaming (Phase 2)  
- `d3570af` feat(admin-ui): chain step boundaries — split streaming messages at tool_call events (Phase 3)  
- `5aacf45` docs: update Ticket 057 status to Implemented

---

## What Changed

The admin chat UI now shows tool *calls* — the agent's outgoing requests — alongside tool *results*. Previously only results were visible: you'd see what a tool returned, but not what the agent asked for. Multi-step chains (agent → call tool → get result → agent continues) also render as distinct message bubbles during streaming instead of concatenating all text into one block.

### Before

Streaming a multi-step chain:

```
[assistant] "Let me check thatHere is what I found"
[tool]      email result
```

One assistant bubble with text before and after the tool call concatenated. Tool calls invisible. History looked different from streaming (separate turns vs. one merged blob).

### After

```
[assistant] "Let me check that..."
[tool_call] email → list_inboxes     ← amber, collapsible
[tool_result] inbox list              ← teal, collapsible
[assistant] "Here is what I found"
```

Each chain step is a separate bubble. Tool calls appear as amber collapsible blocks with formatted JSON arguments. Streaming and history render identically.

---

## Phase 1 — History Rendering

**Scope:** Frontend only, no backend changes.

**What it does:** When loading session history, if an assistant turn has `tool_calls` in its `SessionTurn`, the frontend now emits a `tool_call` UiMessage for each call before the corresponding `tool` result turn.

**Key changes:**

- `UiMessage` role type extended: `'user' | 'assistant' | 'system' | 'tool' | 'tool_call'`
- `toolCalls` field added to `UiMessage` interface (for potential future pairing of calls with results)
- `loadSessionHistory()` now iterates turns and expands `tool_calls` arrays into separate `tool_call` role messages
- Template `v-if`/`v-else-if` chain: `tool_call` → `tool` → `bubble`
- CSS: amber gradient background for tool call blocks (`rgba(245, 158, 11, 0.12)` → `rgba(234, 88, 12, 0.08)`) distinct from teal tool results
- i18n: `chat.toolCall.label` added (English: "Tool Call", Dutch: "Toolaanroep")

**Data flow:** Backend already stores `tool_calls` on assistant turns and returns them via the history API. Phase 1 simply renders what was already there but ignored.

---

## Phase 2 — Live Streaming

**Scope:** Full stack (backend + frontend).

**What it does:** Adds a `tool_call` WebSocket event that fires *before* tool execution, so the UI can show "Calling email..." immediately during a live session.

**Backend changes:**

- `ChainRunner.h`: New `ChainToolCallCallback` type (`std::function<void(const ToolCall&)>`), `m_toolCallCallback` member, `SetToolCallCallback()` setter — following the same pattern as `m_thinkingCallback`
- `ChainRunner.cpp`: In `ProcessResponse`, `m_toolCallCallback(tc)` is called right before `handler->Execute(tc)` — after argument injection (session context, file policy) but before execution
- `ChatSessionService.cpp`: New `toolCallCallback` lambda that emits `{type: "tool_call", session_id, tool_call_id, tool_name, arguments}` via WebSocket, wired with `chainRunner->SetToolCallCallback()`

**WebSocket event format:**

```json
{ "type": "tool_call", "session_id": "42", "tool_call_id": "call_abc", "tool_name": "email", "arguments": "{\"action\":\"list_inboxes\"}" }
```

Sent immediately before tool execution. The existing `tool_result` event (sent after execution) is unchanged.

**Frontend changes:**

- New `tool_call` handler in `handleSocketMessage` that pushes a `tool_call` UiMessage
- Note: `arguments` is a reserved word in JavaScript (the `arguments` object) — variable renamed to `toolArguments` in the handler

---

## Phase 3 — Chain Step Boundaries

**Scope:** Frontend only.

**What it does:** When a `tool_call` event arrives during streaming, the current streaming assistant message is finalized (parser flushed, `streaming = false`) and the `streamingMessageIdBySession` entry is cleared. When the next `token` event arrives after the tool result, `findOrCreateStreamingAssistantMessage()` creates a fresh message.

**The key insight:** `findOrCreateStreamingAssistantMessage()` reuses an existing message if `streamingMessageIdBySession[sid]` is set. By clearing that entry at chain step boundaries, each step naturally gets its own bubble. No structural changes to the streaming architecture — just one strategic `delete` at the right moment.

**Flow during a multi-step chain:**

1. User sends message → `done` event clears old streaming state
2. LLM streams text → `token` events create/reuse streaming message A
3. LLM decides to call a tool → `tool_call` event arrives:
   - Finalize message A (flush parser, `streaming = false`, collapse thinking)
   - Delete `streamingMessageIdBySession[sid]`
   - Push `tool_call` UiMessage
4. Tool executes → `tool_result` event pushes result UiMessage
5. LLM streams more text → `token` events create fresh streaming message B
6. LLM finishes → `done` event finalizes message B

**Edge case handled:** If the streaming message has no text content yet when a `tool_call` arrives (e.g., the LLM's first action is a tool call with no preamble), the finalize logic still runs safely — flushing an empty parser state and setting `streaming = false` on an empty message is harmless. The empty message renders as a no-op (the `v-else-if="message.content"` check skips it in the template).

---

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Call block color | Amber/orange gradient | Warm "outgoing" tone distinct from teal "incoming" results |
| Call block default state | Collapsed (just tool name) | Arguments can be large; collapse keeps the conversation scannable |
| Arguments formatting | `formatToolContent()` pretty-print | Reuses existing JSON pretty-printer; consistent with result formatting |
| Call ID display | Stored but not rendered | Opaque strings like `call_abc123` — not useful to users, kept for potential debug tooltip |
| Phase 3 approach | Clear streamingMessageId at boundary | Simpler than creating new message types; leverages existing `findOrCreate` logic |
| Empty step handling | Harmless no-op | Empty assistant messages are filtered by `v-else-if="message.content"` |

---

## Files Changed

| File | Phase | Change |
|------|-------|--------|
| `include/animus_kernel/ChainRunner.h` | 2 | `ChainToolCallCallback` type, `m_toolCallCallback` member, `SetToolCallCallback()` setter |
| `src/kernel/chain/ChainRunner.cpp` | 2 | `m_toolCallCallback(tc)` call before `handler->Execute(tc)` |
| `src/kernel/admin/ChatSessionService.cpp` | 2 | `toolCallCallback` lambda, `SetToolCallCallback()` wiring |
| `admin-ui/src/views/ChatView.vue` | 1,2,3 | `tool_call` role, history expansion, WS handler, step boundary finalization, CSS |
| `admin-ui/src/i18n/locales/en.ts` | 1 | `chat.toolCall.label` |
| `admin-ui/src/i18n/locales/nl.ts` | 1 | `chat.toolCall.label` |

---

## Resolved Open Questions

1. **Call block color:** Amber/orange for calls, teal for results. ✅ Implemented.
2. **Arguments formatting:** Pretty-print, collapsible. ✅ Uses existing `formatToolContent()`.
3. **Phase 3 step boundaries:** Each chain step gets its own assistant bubble. ✅ Implemented via streamingMessageId clearing.
4. **Tool call IDs in UI:** Not rendered in main view. Available in `toolCallId` field for future tooltip/debug use.

---

## Testing Notes

- **History rendering:** Load any session that has tool calls in its history. Assistant turns with `tool_calls` should show amber call blocks before teal result blocks.
- **Live streaming:** Send a message that triggers tool use. The `tool_call` event should arrive immediately before execution, showing as an amber block with "Tool Call" label and tool name.
- **Chain step boundaries:** During a multi-step chain, each text segment should appear as a separate assistant bubble, not concatenated. The transition should be: assistant text → tool call block → tool result block → assistant text.
- **Empty first step:** If the LLM's first action is a tool call with no text preamble, no empty assistant bubble should appear.