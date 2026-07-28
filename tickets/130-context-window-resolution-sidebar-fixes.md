# Ticket 130: Context Window Resolution & Sidebar Fixes

**Status:** Scoped  
**Date:** 2026-07-28  
**Author:** Kestrel + Melvin  
**Component:** Admin Server, Chat UI, Agent Config

## Problem

The context window system has several interconnected issues:

1. **Context sidebar shows wrong data.** The `/chat` right panel "Context" section calls `/api/v1/sessions/{id}/context`, which returns `consumed_tokens = 0` (hardcoded) and uses `tokenBudgetPerPrompt` (200000) instead of the resolved context window. This produces the misleading `Budget: 200000/200000` display.

2. **Hidden `agent.context_window` field.** The agent's `context_window` column in the database is actively used by `ResolveContextWindow()` as a clamp (via `std::min`), but is **not surfaced anywhere in the admin UI**. A value of 30000 left from compaction testing is silently overriding the 200k that everything else is configured to.

3. **Mismatch between visible and active limits.** The admin UI shows `token_budget_per_prompt` (200k) in the agent's budget section, giving the impression this controls the context window. It doesn't — or at least not in the same way `context_window` does. The relationship between these two fields is unclear and needs clarification.

4. **Sidebar placeholders.** `memory_layers_loaded` and `tools_available` return empty arrays. The placeholder note `"ChainRunner/memory/tool runtime not yet implemented; current context is empty."` is outdated.

## Root Cause Analysis

### The context sidebar endpoint (`/api/v1/sessions/{id}/context`)

File: `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc`, line ~694

```
out["token_budget_state"]["consumed_tokens"] = 0;                    // HARDCODED
out["token_budget_state"]["remaining_tokens"] =                       // = budget - 0
    agentSnapshot.budget.tokenBudgetPerPrompt;                        // WRONG SOURCE
out["memory_layers_loaded"] = Json::Value(Json::arrayValue);         // EMPTY
out["tools_available"] = Json::Value(Json::arrayValue);              // EMPTY
out["note"] = "ChainRunner/memory/tool runtime not yet implemented"; // OUTDATED
```

### The context window resolution chain (`ResolveContextWindow`)

File: `src/kernel/AgentKernel.cpp`, line ~1405

```
1. Start: 128000 (hardcoded fallback)
2. Provider's model-specific context window (from capability detection)
3. Provider's defaultContextWindow (manual config)
4. Agent's context_window field (from DB) — acts as MIN clamp
```

The agent's `context_window` field is the silent clamp. In the working copy it's set to 30000, which overrides everything above it.

### The token gauge endpoint (`/api/v1/sessions/{id}/token-estimate`)

This endpoint properly calls `ResolveProviderContextWindow` with both provider state and agent context window. It returns the real resolved value. This is why the gauge and sidebar disagree.

### The `context_window` vs `token_budget_per_prompt` confusion

| Field | Where stored | What it does | Visible in UI? |
|-------|-------------|--------------|-----------------|
| `agent.context_window` | DB (`agents` table) | Clamps resolved context window via `std::min` in `ResolveContextWindow` | **No** |
| `agent.budget.token_budget_per_prompt` | DB (agent config JSON) | Intended as per-prompt token budget for ChainRunner | **Yes** (AgentsView budget section) |

The `context_window` field was noted as "Legacy field" in `AgentStore.h`:
```
std::uint32_t context_window{128000}; // Legacy field; context is now resolved at provider/model level.
```

But it's NOT legacy — `ResolveContextWindow` still reads and clamps with it. The comment is wrong and the field is silently active.

## Proposed Changes

### 1. Surface `context_window` in the agent UI
- Add a "Context window limit" field to the AgentsView form (budget section or a new context section)
- Label clearly: this is an optional upper bound on the resolved context window
- Value of 0 = "no agent-level cap, use provider/model default"
- Default new agents to 0 (uncapped) rather than 128000

### 2. Fix the context sidebar endpoint
- **Token budget:** Replace `tokenBudgetPerPrompt` with the resolved context window (use same `ResolveProviderContextWindow` path as token-estimate endpoint)
- **Consumed tokens:** Compute real usage — session token estimate (from existing `TokenEstimate::Estimate` on session turns + system prompt) + current unsent user input
- **Alternatively:** Have the sidebar call the token-estimate endpoint directly, since it already computes everything needed
- **Layers:** Wire to actual memory layers loaded for the agent
- **Tools:** Wire to the agent's enabled tool list
- **Remove** the outdated placeholder note

### 3. Unify into a single context window field

**Decision (Melvin):** One configurable limit at the agent level. No more split between `context_window` and `token_budget_per_prompt`.

- **Single field:** `context_window` on the agent — the agent's configured context window limit
- **Resolution:** Provider-level and model-level limits can still override (via `std::min`) if they're smaller. The agent sets the ceiling; the stack resolves downward.
- **`token_budget_per_prompt`** is removed (or repurposed if it serves ChainRunner budget enforcement differently — need to verify usage before removing)
- **UI:** One field in AgentsView: "Context window" — clear, single source of truth

**Verification result (Kestrel):** `tokenBudgetPerPrompt` is **vestigial**. It's stored in the DB, parsed by AgentManager, serialized by AdminServer, defined in structs — but **never read by ChainRunner, PromptAssembler, CompactionService, or any runtime logic**. Grep confirms zero consumption outside storage/config plumbing. Safe to retire.

This means:
- The field users see and configure in AgentsView ("token budget / prompt": 200k) does **nothing**
- The field that actually controls the context window (`context_window`: clamped to 30k in working copy) is **invisible**
- The unification is effectively: surface the field that works, retire the one that doesn't

### 4. Fix the default value
- `context_window` defaults to 128000 in code and DB schema
- If the intent is "no agent cap," the default should be 0 (uncapped), not 128000
- Existing agents with 128000 aren't harmful (most models are ≤128k), but the 30000 test value demonstrates the silent-clamp problem

## Acceptance Criteria

- [ ] Context sidebar shows real consumed/total token counts (not hardcoded 0 / 200000)
- [ ] Context sidebar total matches the token gauge total
- [ ] `agent.context_window` field is visible and editable in the AgentsView form
- [ ] `context_window` default changed from 128000 to 0 (uncapped) for new agents
- [ ] "Legacy field" comment on `context_window` corrected — it's active, not legacy
- [ ] Memory layers and tools show real data in sidebar (or are removed if out of scope)
- [ ] Placeholder note removed from context sidebar
- [ ] `token_budget_per_prompt` removed or explicitly repurposed (verify all usages first)
- [ ] Single `context_window` field is the sole agent-level context limit

## Files Affected

**Backend:**
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc` — context endpoint rewrite
- `include/animus_kernel/AgentStore.h` — fix comment, change default
- `src/kernel/agent/AgentStore.cpp` — DB schema default change
- `src/kernel/AgentKernel.cpp` — `ResolveContextWindow` (may need adjustment based on field decision)

**Frontend:**
- `admin-ui/src/views/ChatView.vue` — context sidebar display
- `admin-ui/src/views/AgentsView.vue` — add context_window field to form

## Dependencies

- None (foundational bug fix)
