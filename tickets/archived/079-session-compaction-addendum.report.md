# Ticket 079 Addendum — Compaction Debugging (June 28, 2026)

## Status: Fully Operational

The compaction system was implemented on June 14 but never actually fired in production. A chain of six bugs prevented it from working end-to-end. All six were identified and fixed on June 28.

## Bugs Found and Fixed

### Bug 1: Agent `context_window` not persisted via admin PATCH

**Symptom:** Agent's context window stayed at 128000 (default) despite being set to 30000 via the admin UI.

**Root cause:** `ApplyAgentEntityPatch` in `AgentManager.cpp` parsed `model.provider` and `model.model_id` but silently ignored `model.context_window`. The field was only settable during initial agent creation, not through subsequent PATCH requests.

**Fix:** Added `model.context_window` and top-level `context_window` parsing to `ApplyAgentEntityPatch`.

### Bug 2: No agent-level context window clamp

**Symptom:** Even when `context_window` was correctly set in the DB, the LLM call used the provider's full context window (200k for Ollama).

**Root cause:** `ChatSessionService`, `AgentKernel`, and the token-estimate endpoint all resolved the provider's context window but never checked the agent's `context_window` as an upper bound. The field was marked "legacy" and unused.

**Fix:** Added `min(agent.context_window, provider_window)` clamping at all three call sites:
- `ChatSessionService.cpp` (ws_chat path)
- `AgentKernel.cpp` (scheduled/consolidation/channel dispatch paths)
- `AdminServerRoutesInterfacesSessionsMemory.inc` (token-estimate endpoint)

### Bug 3: `min_messages_before_compact` too high

**Symptom:** Compaction didn't trigger even when the session was well over the token threshold.

**Root cause:** `min_messages_before_compact` defaulted to 20. With a 30k context window and dense conversational turns (long paragraphs), 27k tokens of content could fit in 11 turns. The 20-turn minimum blocked compaction.

**Fix:** Lowered default from 20 to 6. The token threshold (80% of context window) is the real guard; the turn minimum only prevents compacting trivially short sessions.

### Bug 4: `DoCompact` re-estimated tokens and bailed

**Symptom:** `needs_compaction=true` was set by `PromptAssembler` but `DoCompact` returned "no compaction needed."

**Root cause:** `CompactIfNeeded` passed `force=false` to `DoCompact`. `DoCompact` then independently re-estimated total tokens using only `systemPrompt + turn content` (char/3 estimate). This missed all context provider tokens (active memory, session reports, ontology, perspectives, memory files) which contributed the bulk of the token budget. `DoCompact` saw ~7k tokens vs `PromptAssembler`'s ~28k tokens, and bailed.

**Fix:** `CompactIfNeeded` now passes `force=true` to `DoCompact`, skipping the redundant budget check. The compaction decision is already made upstream with a more accurate estimate.

### Bug 5: Compacted turns not removed from session

**Symptom:** Compaction ran, stored a summary, but the session didn't shrink — the same turns remained and compaction triggered again on the next message.

**Root cause:** `DoCompact` called `session.SetCompactionSummary(summaryTurn)` but never removed the compacted turns from the session's turn list. `PersistSession` then re-inserted all turns (including compacted ones) with `is_summary=0`. On the next prompt assembly, both the summary and the original turns were included.

**Fix:** Added `session.TrimOldestTurns(compactEnd)` after setting the compaction summary, removing the compacted turns from the in-memory session before persisting.

### Bug 6: `AgentKernel` execution paths never checked `triggered_compaction`

**Symptom:** Consolidation intake sessions (running through `AgentKernel`, not `ChatSessionService`) showed `needs_compaction=true` on every assembly but never compacted.

**Root cause:** `ChatSessionService` had the post-response compaction trigger (`if result.triggered_compaction && compactionService → CompactIfNeeded`), but `AgentKernel`'s four `ExecuteOnSession` call sites (channel dispatch, consolidation intake, scheduled sessions, ConsolidationTool direct) all just called `FlushSession` and moved on.

**Fix:** Added `triggered_compaction` check and `CompactIfNeeded` call to all four `AgentKernel` execution paths.

## UI Enhancement: Compaction Timeline

Added a compaction timeline to the chat context sidebar:
- Vertical timeline with purple dots showing each compaction event
- Each entry displays timestamp, truncated summary (120 chars), token count, and model
- Clicking an entry opens a modal dialog with the full summary rendered as markdown
- Fetches from `GET /api/v1/sessions/{id}/compactions` on session switch and after generation completes
- Card only appears when compactions exist for the current session

## Commits (June 28 debugging session)

| Commit | Fix |
|--------|-----|
| `0a8958a` | Lower `min_messages_before_compact` from 20 to 6 |
| `e94e2e6` | `CompactIfNeeded` passes `force=true` to skip redundant re-estimation |
| `2603b70` | `TrimOldestTurns` removes compacted turns from session |
| `36f1f40` | Add compaction trigger to all `AgentKernel` execution paths |
| `eb1ca8d` | Compaction timeline UI in chat context sidebar |
| Earlier: agent `context_window` persistence + clamping fixes | Bugs 1 and 2 |

## Verification

- Session 104: 21 turns → 10 turns after compaction (11 turns compacted into summary)
- 3 compaction entries stored in `session_compactions` table
- Token estimate correctly shows 30k window (clamped from 200k)
- Compaction timeline visible in chat UI context sidebar
- `needs_compaction` triggers at 80% (24k of 30k)
- Compaction summary included in subsequent prompt assemblies via `GetCompactionSummary()`

## Remaining Items

- **Non-chat paths use hardcoded 128000:** The `AgentKernel` consolidation/scheduled/channel paths pass `128000` as context window to `ExecuteOnSession`. These should resolve the agent's `context_window` like `ChatSessionService` does.
- **Background compaction:** Compaction still runs synchronously. For very long sessions this blocks the response.
- **Cascading compaction:** Multiple compactions stored independently; older ones could be merged.
- **Compaction preview:** No UI for previewing what will be compacted before triggering.
- **Diagnostic logging:** Debug traces added during investigation should be removed before release.