# Ticket 089: Context Window Token Gauge

## Status: Implemented
## Priority: Medium (observability — needed before compaction debugging)
## Created: 2026-06-16

## Problem

There is no way to see how full the context window is from the admin UI. The `/api/v1/sessions/{id}/context` endpoint is a stub returning hardcoded zeros. Before debugging compaction behavior, we need visibility into when the compaction threshold is crossed.

The desired UX: a token usage gauge to the left of the "Connected" status indicator in the chat toolbar, showing current estimated token usage vs. the model's context window maximum.

## Design

### Approach

Add a new endpoint that performs "dry-run" prompt composition — assembling the full system prompt + session turns + compaction summary + (optionally) a draft user message, computing the estimated token count, and returning it without sending anything to the LLM.

### Endpoint

```
GET /api/v1/sessions/{sessionId}/token-estimate
```

**Query params:**
- `draft` (optional) — draft user message text to include in the estimate, so the gauge updates as the user types

**Response:**
```json
{
  "session_id": 42,
  "estimated_tokens": 15384,
  "context_window": 128000,
  "compaction_threshold": 0.8,
  "compaction_trigger_tokens": 102400,
  "needs_compaction": false,
  "breakdown": {
    "system_prompt": 4200,
    "compaction_summary": 0,
    "session_turns": 11100,
    "draft_message": 84
  }
}
```

### Implementation steps

1. **Flesh out the stub** at `/api/v1/sessions/{id}/context` (or create new `/api/v1/sessions/{id}/token-estimate` — prefer a new endpoint to keep the stub as-is for backward compat).

2. **Server-side assembly:**
   - Resolve agent from session's `agent_id`
   - Resolve provider + model from session/agent/provider chain (same logic as `ChatSessionService`)
   - Resolve context window via `ResolveProviderContextWindow`
   - Assemble context blocks via `m_contextRegistry->Assemble(agent, sessionAccess)` → system prompt
   - Load compaction summary from `CompactionService::GetLatestCompaction(sessionId)`
   - Call `PromptAssembler::BuildFromAccess(session, draftMessage, systemPrompt, model, contextWindow, threshold)`
   - Use the `TokenEstimator` already embedded in `PromptAssembler` for the breakdown — or better, make `BuildFromAccess` optionally return per-section token counts in `PromptAssemblyResult`

3. **Per-section breakdown:** Add token counts to `PromptAssemblyResult`:
   ```cpp
   struct PromptAssemblyResult {
       bool needs_compaction{false};
       llm::LLMRequest request;
       std::size_t system_prompt_tokens{0};
       std::size_t compaction_summary_tokens{0};
       std::size_t session_turns_tokens{0};
       std::size_t draft_message_tokens{0};
       std::size_t total_tokens{0};
   };
   ```
   These are computed during `Build()` when iterating the message list. Trivial to add — just accumulate `m_estimator.Estimate()` per section.

4. **Frontend — polling:**
   - Add a `tokenEstimate` ref to ChatView
   - Poll `/api/v1/sessions/{id}/token-estimate?draft=<currentInput>` every ~3-5 seconds when:
     - A session is active (not "new")
     - The WebSocket is connected
     - The tab is visible
   - Debounce on input change (don't poll on every keystroke)
   - Stop polling when generating

5. **Frontend — gauge UI:**
   - Position: left of the "Connected" status chip in `.toolbar-right`
   - Component: a thin progress bar or `v-progress-linear` (horizontal, ~80px wide, ~6px tall)
   - Color states:
     - Green (< 60% of context window)
     - Amber (60–80%)
     - Red (> 80%, approaching compaction threshold)
   - Tooltip: `"15,384 / 128,000 tokens (12%)"` + per-section breakdown
   - Show percentage label to the right of the bar in small text

6. **Debounce strategy:**
   - Input changes: debounce 500ms, then fetch estimate with current draft text
   - After message send/receive: fetch immediately (session state changed)
   - During generation: suppress polling (prompt is being streamed)
   - Tab hidden: suppress polling (visibilitychange listener)

### Files to modify

| File | Change |
|------|--------|
| `include/animus_kernel/PromptAssembler.h` | Add per-section token fields to `PromptAssemblyResult` |
| `src/kernel/chain/PromptAssembler.cpp` | Populate token breakdown during `Build()` |
| `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc` | New endpoint handler |
| `admin-ui/src/views/ChatView.vue` | Polling logic + gauge component |
| `admin-ui/src/views/ChatView.vue` | TypeScript interfaces for response |

### Out of scope

- Tokenizer-accurate counting (we use the existing 4-chars-per-token estimator)
- Compaction trigger integration (visualization only)
- Per-message token breakdown in the UI (aggregate is enough)

## Testing

1. Open a chat session, type a message → gauge should update within ~1s
2. Have a long conversation → gauge should climb steadily
3. Verify gauge matches `needs_compaction` flag when crossing 80% threshold
4. Switch sessions → gauge should reflect the new session's token usage
5. Switch models/providers → context window should update accordingly
