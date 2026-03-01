# AGENTS.handoff.md — Deferred Implementation Items

> **Purpose:** Compact handoff for fresh context / Codex agent dispatch.
>
> **Created:** 2026-03-01
>
> **Status:** These items are documented in architecture but not yet implemented. They can be picked up independently.

---

## 1. Multi-Session Context Routing

### What's done
- `ISessionRouter` interface exists (`include/animus_kernel/ISessionRouter.h`)
- `DefaultSessionRouter` derives primary session key from connector metadata (`channel_id + thread_ts`, `conversation_id`, etc.)
- `SessionContextSet` struct supports `{ primary_session_id, context_session_ids[] }`

### What's missing
- `DefaultSessionRouter` currently returns **only a primary session**; `context_session_ids[]` is always empty.
- No routing rule system exists yet to pull in additional sessions as read-only context.

### Design intent (from `AGENTS.system_draft.md`)
- **Default**: route to a *primary Session* derived from connector metadata.
- **Configurable routing**: allow an event to also pull in *additional Sessions as context sources* (multi-session context assembly).
- **Routing rules**: future system for linking/including multiple Sessions as context (e.g., "include project X's session when in channel Y").

### Implementation outline
1. Extend `DefaultSessionRouter` or create `RuleBasedSessionRouter`:
   - Accept a list of routing rules (config-driven).
   - Each rule: match condition (e.g., channel_id prefix, metadata key) → add context session id.
2. Return both `primary` and populated `context[]` in `SessionContextSet`.
3. Update `SessionManager` to load context sessions alongside primary.

### Files to modify
- `include/animus_kernel/ISessionRouter.h`
- `src/kernel/session/DefaultSessionRouter.cpp`
- `src/kernel/session/SessionManager.cpp`
- Possibly new: `include/animus_kernel/SessionRoutingRule.h`

---

## 2. Write Policy for Context Sessions

### What's done
- `Session` supports `AddTurn()` for writing.
- `SessionManager` returns a `SessionContextSet` with primary + context sessions.

### What's missing
- No enforcement that **writes go only to primary Session**.
- Nothing prevents code from calling `AddTurn()` on a context session.

### Design intent
- **Safety default**: writes (new turns, summaries/compactions) go **only to the primary Session** unless a routing rule explicitly says otherwise.
- Prevents accidental cross-contamination between conversation contexts.

### Implementation outline
1. Add a `SessionAccessMode` enum: `ReadWrite`, `ReadOnly`.
2. `SessionContextSet` becomes:
   ```cpp
   struct SessionContextSet {
       Session* primary;                    // ReadWrite
       std::vector<Session*> context;       // ReadOnly by default
   };
   ```
3. Either:
   - Wrap context sessions in a read-only proxy that throws on `AddTurn()`, OR
   - Add a runtime check in `SessionManager` / calling code.

### Files to modify
- `include/animus_kernel/Session.h` (or new `SessionAccessMode.h`)
- `include/animus_kernel/SessionManager.h`
- `src/kernel/session/SessionManager.cpp`

---

## 3. Conversation History Compaction

### What's done
- `Session` stores `SessionTurn` objects (role + content + timestamp + metadata).
- `SessionTurn` structure supports provenance tracking.

### What's missing
- No compaction logic.
- No `ConversationCompactor` component.
- No rolling summary storage in `Session`.

### Design intent (from `AGENTS.system_draft.md` + `AGENTS.todo.md`)
- **Compaction trigger**: token pressure / time / turn count.
- **Compaction method**: optional LLM-driven sub-prompt that produces a rolling summary.
- **Storage**: compaction outputs stored in `Session` with provenance (e.g., `is_summary: true`, `compacted_from: [turn_ids]`).
- **Prompt assembly**: when assembling prompt, include rolling summary instead of full history when threshold exceeded.

### Implementation outline
1. Add `CompactionPolicy` struct:
   - `maxTurnsBeforeCompaction` (e.g., 50)
   - `maxTokenEstimateBeforeCompaction` (e.g., 100k)
   - `compactionStrategy`: `Truncate` | `Summarize`
2. Add `ConversationCompactor` class:
   - Takes a `Session` and policy.
   - Produces a summary turn (if `Summarize`) or truncates old turns.
   - Stores summary with provenance.
3. Add `Session::GetCompactionSummary()` for prompt assembly.
4. Integrate into `PromptAssembler` (Milestone 6).

### Files to add/modify
- New: `include/animus_kernel/CompactionPolicy.h`
- New: `include/animus_kernel/ConversationCompactor.h`
- New: `src/kernel/session/ConversationCompactor.cpp`
- Modify: `include/animus_kernel/Session.h` (add summary storage)
- Modify: `src/kernel/session/Session.cpp`

---

## Dependencies Between Items

- **Item 1** (multi-session routing) and **Item 2** (write policy) are related: if we add context sessions, we need the write policy to prevent accidental writes.
- **Item 3** (compaction) is independent — can be implemented standalone.

---

## Suggested Dispatch Order

1. **Item 2** (write policy) — small, self-contained, low risk.
2. **Item 3** (compaction) — independent, well-scoped.
3. **Item 1** (multi-session routing) — larger, benefits from having write policy in place first.

---

## Reference Docs

- Architecture: `AGENTS.system_draft.md`
- Task tracking: `AGENTS.todo.md`
- Session code: `include/animus_kernel/Session.h`, `src/kernel/session/Session.cpp`
- Router code: `include/animus_kernel/ISessionRouter.h`, `src/kernel/session/DefaultSessionRouter.cpp`
- Manager code: `include/animus_kernel/SessionManager.h`, `src/kernel/session/SessionManager.cpp`
