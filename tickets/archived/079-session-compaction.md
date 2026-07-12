# Ticket 079 — Session Compaction

**Status:** Implemented  
**Date:** June 14, 2026  
**Commits:** `69dc3be` → `b2019df` → `4143311` → `b5d45cf`  
**Files changed:** 24 | **Lines:** ~860 insertions

---

## Problem

Long agent sessions exceed the LLM context window. Once a session's token count passes the provider's limit, the chain runner can't assemble a valid prompt. Sessions either truncate silently or fail outright. No mechanism existed to compress session history into a manageable summary while preserving continuity.

## Solution

A three-layer compaction system that acts when sessions cross 80% of the configured context window:

### Layer 1: Structural Summary (Fallback)

`CompactionService` generates a minimal structural summary from turn metadata — role, content preview, timestamps. No LLM call required. Always available as a baseline.

### Layer 2: LLM Summary (Enhancement)

`CompactionSummaryGenerator` calls the session's configured LLM provider with a structured summarization prompt covering six domains:

1. **Decisions made** — what was decided and why
2. **Actions taken** — files modified, commands run, outcomes
3. **Current state** — what exists now (files, config, processes)
4. **Open issues** — unresolved problems, blocked tasks, pending questions
5. **Key context** — names, IDs, paths, credentials, versions referenced later
6. **Emotional/narrative state** — interpersonal dynamics worth preserving

Long sessions use a head-5 + tail-50 strategy, omitting the middle with a count marker. Each message truncated at 2000 chars.

Falls back gracefully: if the LLM provider is unavailable or returns an error, the structural summary is used instead.

### Layer 3: DB Persistence + In-Memory Assembly

Compaction summaries are stored in `session_compactions` (id, session_id, message_id, summary, token_count, model, created_at). `PromptAssembler` loads the latest compaction from DB before assembling the prompt, prepending it as system context. Subsequent turns after the compaction point are included normally.

Each layer fails independently. No single point of failure.

## Architecture

```
Session turns → TokenEstimator → threshold check (80%)
                                    ↓ triggers
                           CompactionService.CompactIfNeeded()
                                    ↓
                    ┌─ LLM available? ──→ CompactionSummaryGenerator
                    │                        ↓
                    │              LLM structured prompt → summary
                    │                        ↓
                    └─ Fallback ──→ Structural summary
                                     ↓
                        Store in session_compactions
                                     ↓
                        Mark turns as is_summary, cache token_count
                                     ↓
                        PromptAssembler loads compaction on next turn
```

**Key design decision:** Compaction keeps the last 10 turns un-compacted for recent context. The summary covers everything before that window. This means the prompt always has: compaction summary + last 10 turns + new user message.

## Implementation Details

### Backend

- **`CompactionService`** — Core service: threshold detection, cascading compaction support, DB reads/writes
- **`CompactionSummaryGenerator`** — LLM-backed summary generation with `ProviderConfigLookup` for proper provider resolution
- **`session_compactions`** table — Migration v3: id, session_id, message_id, summary, token_count, model, created_at
- **`token_count`** column on `session_turns` — Migration v4: caches estimated token count per turn
- **Admin API** — 3 endpoints:
  - `GET /api/v1/sessions/{id}/compactions` — list all compactions
  - `GET /api/v1/sessions/{id}/compactions/latest` — get newest compaction
  - `POST /api/v1/sessions/{id}/compact` — trigger manual compaction
- **SessionsTool** — 3 actions: `compactions`, `compactions:latest`, `compact`
- **`ChainRunner::GetConfigLookup()`** — New accessor exposing provider config resolution for reuse by compaction generator

### Frontend

- **Compaction markers** — Summary turns render as 📃 collapsible markers with indigo/violet styling
- **`UiMessage.isSummary`** — New boolean flag on the UI message type
- **`compaction` role** — New message role for rendering compaction summaries distinctly from regular system messages
- **Show/Hide toggle** — Compaction detail is collapsed by default, expandable on click

### Wiring

- `ChatSessionService` creates `CompactionSummaryGenerator` with the current provider's config lookup when `triggered_compaction` fires after chain execution
- `PromptAssembler` detects compaction summaries and prepends them to the assembled prompt
- `AgentKernel` exposes compaction endpoints through the admin API router

## Pre-existing Scaffolding

Much of the infrastructure already existed — the implementation gap was the handler that acted on the trigger signal:

- `SessionTurn.is_summary`, `compacted_from` fields — already in schema
- `Session.SetCompactionSummary/GetCompactionSummary` — already on the model
- `CompactionPolicy` enum — already defined
- `PromptAssembler` compaction detection + inclusion — already wired
- `ChainRunner.triggered_compaction` flag — already set after chain execution
- `TokenEstimator` — already functional

The work was connecting these pieces into a working pipeline and adding the LLM generation layer on top.

## Commits

| Hash | What |
|------|------|
| `69dc3be` | Core: `CompactionService` class, `session_compactions` DB table, threshold detection (80%), keeps last 10 turns, 3 admin API endpoints, wired into ChatSessionService + AgentKernel |
| `b2019df` | Token caching (`token_count` on `session_turns`, migration v4), PromptAssembler uses cached count, ChatSessionService loads latest compaction, SessionsTool: `compactions`/`compactions:latest`/`compact` actions |
| `4143311` | Frontend: compaction summary turns render as 📃 collapsible markers, `UiMessage.isSummary` + `compaction` role |
| `b5d45cf` | LLM summary generation: `CompactionSummaryGenerator` with structured prompt, head-5 + tail-50 strategy, graceful fallback, `ChainRunner::GetConfigLookup()` |

## Testing

Manual testing via admin UI. Verified:
- Compaction triggers when session token count exceeds 80% threshold
- LLM summary generation produces structured summaries
- Fallback to structural summary works when LLM unavailable
- Frontend renders compaction markers with expand/collapse
- SessionsTool `compact` action triggers compaction on demand
- SessionsTool `compactions`/`compactions:latest` return stored summaries

## Open Items

- **Background job:** Compaction currently runs synchronously in the chat session thread. For very long sessions, this could block the response. Future: offload to JobSystem.
- **Cascading compaction:** Multiple compactions on the same session are stored independently. Future: merge older compactions into newer ones when they overlap significantly.
- **Compaction preview:** No UI for previewing what will be compacted before triggering. Future: show the turn range and estimated token savings.