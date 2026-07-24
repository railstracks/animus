# Ticket 128: Architecture Hardening — Policy Centralization Pass

**Status:** Draft
**Date:** 2026-07-25
**Author:** Kestrel (based on audit by Melvin + external reviewer)

## Problem

Animus has a strong architectural core, but several high-growth files have become multi-role hubs carrying lifecycle, policy, routing, persistence, and feature wiring in the same place. The primitives are sound; the risk is that successful seams are being bypassed or overloaded by feature-specific wiring.

An architecture-oriented source audit identified 10 findings. After cross-referencing the audit against known bugs and active issues, six items are worth picking up for the next release. Two additional items (not in the audit) are included because they are active user-facing bugs that the audit missed.

## Scope

This ticket covers the next release hardening pass. Items are ordered by priority — correctness first, then consistency, then hygiene.

---

## 1. IDataStore / IStatement DML Semantics *(correctness)*

**Audit Finding 4.** `IStatement::Step()` is documented as "true if a row is available, false if done or error." SQLite returns `false` for successful INSERT/UPDATE/DELETE (`SQLITE_DONE`). PostgreSQL's `PgStatement::Step()` returns `true` once for `PGRES_COMMAND_OK`. This makes `Step()` mean different things by backend.

**Already caused damage:** `AuthStore` had 8 calls to nonexistent `stmt->Exec()`. Write methods that `return stmt->Step()` are unreliable across backends.

**Work:**
- Split statement API: `StepRow()` for SELECT iteration, `ExecuteStatement()` for DML
- Or standardize `Step()` to mean "row available" only; require write success via explicit `RowsAffected()`
- Audit all `return stmt->Step()` write methods (`GallivantingStore::UpdateThread`, `AuthStore::CreateUser`, `AuthStore::UpdateUserPassword`, `SqliteSessionStore::PersistSession`, others)
- Add backend parity tests for representative INSERT/UPDATE/DELETE return values

## 2. Shared Execution Request Resolver *(consistency)*

**Audit Finding 2.** The project has multiple execution paths (web chat, channel dispatch, scheduler, consolidation) that each perform their own version of provider/config/model/context-window resolution, session setup, compaction handling, and flushing. This has already caused real bugs around context-window clamping and compaction triggers.

**Work:**
- Introduce `ExecutionRequestResolver` (or `AgentRuntimeResolver`)
- Resolve once per activation: agent, provider instance ID, provider type/registry key, model, context window, reasoning settings, chain budgets, tool budget, prompt log policy
- All entry points (ChatSessionService, AgentKernel::ExecuteChannelDispatch, scheduler callback, ConsolidationPipeline) call the same resolver before invoking ChainRunner
- ChainRunner focuses on chain loop, prompt assembly, LLM call, tool execution, turn storage — receives pre-resolved context

## 3. Extract Tool Schema and Execution Services from ChainRunner *(maintainability)*

**Audit Findings 3 + 9.** `ChainRunner.cpp` is ~1,300 lines containing duplicated streaming/non-streaming logic, agent override resolution, tool definition conversion, tool allowlist filtering, session-type gating, tool config injection, node forwarding, tool result storage, interjection handling, and prompt logging.

Tool capability and permission modeling is half-centralized: `__session_key`, `__agent_id`, `__policy`, `__config`, `__node` are injected as hidden JSON arguments. Schemas shown to the model don't describe runtime behavior.

**Work:**
- Extract `ToolSchemaService` — agent/session-filtered schema generation (replaces inline `GetToolDefinitionsForSession` + `ConvertToolDef` logic)
- Extract `ToolExecutionService` — context injection, node routing, execution, result-mode routing
- Introduce `ToolExecutionContext` struct passed separately from model arguments (agent ID, session key, node routing, file policy, tool config, permissions)
- Replace tool-specific argument injection (`InjectFilePolicy`, `InjectToolConfig`) with a generic pre-execution context/config hook
- Reduce streaming/non-streaming duplication by sharing a per-step helper

## 4. OpenAICompat Serializer Fixes *(correctness — not in audit)*

Two bugs in `src/kernel/llm/OpenAICompat.cpp` that the audit missed but are active user-facing issues:

### 4a. `content: ""` vs `content: null` for assistant messages with tool_calls

When a model returns a tool call with no content (common for Mistral/Qwen), the assistant turn is stored with `content = ""`. The serializer always emits content as a string:

```cpp
ss << ",\"content\":\"" << EscapeJson(msg.content) << "\"";
```

Result: `{"role":"assistant","content":"","tool_calls":[...]}`

The OpenAI spec says `content` should be `null` (not empty string) when `tool_calls` is present. Nemotron tolerates this, but Mistral and Qwen on Ollama Cloud reject it with HTTP 400 `"invalid tool call arguments"`.

**Fix:** When `msg.role == "assistant"` and `msg.content.empty()` and `!msg.tool_calls.empty()`, emit `"content":null` instead of `"content":""`.

### 4b. `ExtractJsonString` incomplete escape handling

`ExtractJsonString` handles `\"`, `\\`, `\n`, `\t`, `\r` but NOT `\uXXXX`, `\/`, `\b`, `\f`. If a model returns tool call arguments with unicode escapes, `ParseToolCalls` corrupts the arguments. When the corrupted arguments are re-serialized into the next chain step's request, the provider rejects them.

The SSE accumulator path (jsoncpp) handles escapes correctly, but `ParseToolCalls` is the fallback when the accumulator doesn't capture the arguments.

**Fix:** Add `\uXXXX`, `\/`, `\b`, `\f` handling to `ExtractJsonString`, or replace `ExtractJsonString` with jsoncpp parsing in `ParseToolCalls`.

## 5. Channel Adapter Boundary *(architecture)*

**Audit Finding 6.** `ChannelManager` owns platform-specific details for 10+ platforms (IRC, Telegram, VK, email, Discord, WhatsApp, Slack, Nextcloud Talk, Bluesky, Twitter) in one class. `ReplyTarget` is accumulating platform-specific fields.

**Work:**
- Introduce `IChannelAdapter` interface: `Start`, `Stop`, `SendReply`, `ValidateConfig`, event callbacks
- `ChannelManager` becomes orchestrator: CRUD, lifecycle, routing, dispatch only
- Move platform loops and reply formatting into adapter classes
- Replace `ReplyTarget` platform field accumulation with typed payload or adapter-owned opaque route token

**Note:** This is the largest scope item. If it doesn't fit in one release, it can be split into a separate ticket. The channel system works today; this is about preventing the adapter code from becoming unmaintainable as platforms are added.

## 6. Structured Logging *(operability)*

**Audit Finding 8.** Many `std::cerr` diagnostics remain in hot paths: `ChainRunner`, `ContextProviderRegistry`, `ActiveMemoryProvider`, session store methods, startup, channel dispatch. Some are investigation traces from recent tickets.

This affects performance, risks leaking prompt/tool metadata, and makes it hard to distinguish expected runtime events from real warnings.

**Work:**
- Add a small structured logging facade with levels (trace/debug/info/warning/error) and categories (chain, context, provider, channel, store, tool)
- Default chain/context/provider logs to warning/error unless prompt logging is explicitly enabled
- Treat prompt/tool payload logging as sensitive and opt-in
- Gate existing `std::cerr` diagnostics behind the appropriate level

## 7. Session Persistence: Append-Only Turn Insertion *(performance)*

**Audit Finding 5.** `SqliteSessionStore::PersistSession()` upserts the session, deletes all turns, and rewrites every turn. Write amplification grows with session length. Concurrent writers can clobber each other. Intake markers and compaction state live in the same table being wholesale rewritten.

**Work:**
- Move toward append-only turn insertion plus targeted `UPDATE` for mutable fields (`intake_processed`, `is_compacted`, `token_count`, summary)
- Keep full-session rewrite as legacy/import path only
- Add per-session write serialization or optimistic versioning before multiple ingress paths become common

**Note:** The `is_compacted` flag fix (v0.2.3) is a band-aid on this exact problem. The full migration is substantial — assess whether it fits this release or should be deferred.

---

## Deferred (not in this ticket)

The following audit findings are valid but lower priority. Filed here for awareness, to be addressed in a future release:

- **AgentKernel bootstrap extraction** (Audit Finding 1) — good hygiene, but won't prevent bugs. The startup modules proposal (`ProviderBootstrap`, `ToolBootstrap`, etc.) is directionally correct but not urgent.
- **Context budget allocator** (Audit Finding 7) — conceptually right but requires touching every `IContextProvider`. Architecture direction, not next-sprint work.
- **Ownership/lifecycle `unique_ptr` cleanup** (Audit Finding 10) — the shutdown watchdog (v0.2.3) is the practical mitigation. Converting raw pointers to `unique_ptr` is hygiene that won't change runtime behavior.

## Priority Summary

| # | Item | Type | Effort |
|---|------|------|--------|
| 1 | IDataStore DML semantics | Correctness | Medium |
| 2 | Execution request resolver | Consistency | Medium |
| 3 | Extract tool services from ChainRunner | Maintainability | Large |
| 4a | `content: null` for assistant+tool_calls | Correctness (1-line fix) | Small |
| 4b | `ExtractJsonString` escape handling | Correctness | Small |
| 5 | Channel adapter boundary | Architecture | Large |
| 6 | Structured logging | Operability | Medium |
| 7 | Session persistence append-only | Performance | Large |

Items 4a and 4b are the smallest and most immediately impactful — they fix the active Mistral/Qwen HTTP 400 bug. Recommend doing those first, then 1-2, then 3/5/6/7 in priority order.