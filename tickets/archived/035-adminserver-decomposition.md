# Ticket 035 — AdminServer Decomposition: Extract Domain Subsystems into Discrete Classes

**Priority:** P1 (architectural health; blocks sustainable feature growth)
**Status:** Complete
**Dependencies:** Ticket 034 (IRC Interface — partial, runtime extraction is part of this ticket)
**Updated:** 2026-05-06

## Summary

Decompose the `AdminServer` god-object into discrete, single-responsibility classes. AdminServer should contain exclusively system-wide core functions and act as the glue that wires route handlers to domain subsystems. All domain logic — interface runtimes, OAuth flows, provider management, agent CRUD, memory operations — moves to dedicated classes.

## Motivation

`AdminServer.cpp` has grown to **8279 lines (336 KB)**. It contains:

1. An entire IRC client runtime (`IrcInterfaceRuntime`, ~400 lines) with raw socket handling, line parsing, reconnect backoff — defined inline in the same file as HTTP route handlers.
2. OAuth device flow + PKCE browser flow logic (~800 lines of HTTP client calls, token exchange, state management).
3. Provider CRUD + connectivity testing + model listing.
4. Agent CRUD with per-agent tool config validation.
5. Memory layer + observation + consolidation route logic.
6. Constitution CRUD + adaptation + audit log.
7. WebSocket controllers (chat + observation stream).
8. Tool registry endpoint.
9. Core admin lifecycle (start/stop/run loop, config load/save).

This is exactly the pattern the 2026-05-03 audit flagged as Finding #5 ("AdminServer god-object with public internals"). Phase 1 "mitigated" it with section comments and an accessor — but the file has since *doubled in size* by absorbing IRC runtime, OAuth flows, and agent tool config.

**The 8 MB RSS of the Animus daemon proves the runtime cost is low. The problem is maintainability and coupling risk, not memory.** Every new interface type, provider, or feature currently requires editing the same 8000-line file with the same mutable state and mutex set. Cross-cutting concerns (error handling, secret masking, validation) are duplicated across route blocks because there's no shared domain boundary to anchor them.

### Why now

- Ticket 034 (IRC) adds a new subsystem with its own socket lifecycle. If it stays in AdminServer, every future interface (Slack, Telegram, Discord) will too.
- The OAuth device/browser flow is provider-protocol logic, not admin routing. It already needs to be reusable for future providers.
- Agent CRUD is acquiring its own validation matrix (tool configs, provider inheritance). It will keep growing.
- Decomposition is cheapest when the code is still in one place. After three more interface types are jammed in, extraction becomes a refactoring quagmire.

## Scope

### In Scope

1. Extract `IrcInterfaceRuntime` to `src/kernel/interfaces/IrcInterface.h` + `IrcInterface.cpp`.
2. Extract provider domain logic to `src/kernel/admin/ProviderManager.h` + `ProviderManager.cpp`.
3. Extract OAuth flow logic to `src/kernel/admin/OAuthManager.h` + `OAuthManager.cpp`.
4. Extract agent CRUD + validation to `src/kernel/admin/AgentManager.h` + `AgentManager.cpp`.
5. Extract memory route logic to `src/kernel/admin/MemoryManager.h` + `MemoryManager.cpp`.
6. Extract constitution route logic to `src/kernel/admin/ConstitutionManager.h` + `ConstitutionManager.cpp`.
7. Refactor `AdminServer` to own only: lifecycle, route registration, request dispatch, and pointer ownership of the extracted managers.
8. Route handlers call into manager methods instead of operating on AdminServer member state directly.
9. Move shared state (mutexes, config maps) into the owning manager. AdminServer holds managers, not raw state.
10. Update `AdminServer.h` to remove moved members and add manager pointers.
11. Update `CMakeLists.txt` for new source files.
12. Existing tests continue to pass (no behavioral change).

### Out of Scope

1. Adding new features (TLS for IRC, SASL, new interface types).
2. Changing the HTTP API surface (endpoints, payloads, status codes remain identical).
3. Refactoring the WebSocket controllers (separate concern, can be a follow-up).
4. Changing the build system beyond adding new source files.
5. Introducing a plugin/module loader for interfaces (future work).
6. Renaming or restructuring the namespace beyond what extraction requires.

## Design Plan

### Architecture After Decomposition

```
AdminServer (glue + lifecycle + route dispatch)
 ├── ProviderManager      — provider CRUD, test, model list, capabilities, state
 ├── OAuthManager          — device flow, PKCE browser flow, token persistence
 ├── AgentManager          — agent CRUD, tool config validation, provider inheritance
 ├── MemoryManager         — layer CRUD, observation CRUD, consolidation, perspective
 ├── ConstitutionManager   — core/contracts/adaptation CRUD, audit log
 ├── IrcInterfaceRuntime   — IRC client lifecycle (owned by InterfaceManager, see below)
 └── InterfaceManager      — interface state, enable/disable, runtime lifecycle (IRC now, Slack/Telegram later)
```

AdminServer holds pointers to managers. Route handlers are thin: parse request → call manager → format response.

### Phase 0: Boundary Contract + Extraction Guardrails

1. Define explicit manager APIs before moving any code:
   - Each manager header declares its public command/query surface.
   - AdminServer may only invoke manager methods, not manager internals.
2. Lock-order contract is documented and enforced:
   - Preferred order: `ProviderManager` → `OAuthManager` → `InterfaceManager` → `MemoryManager` → `ConstitutionManager` → `AgentManager`.
   - No manager acquires another manager's mutex directly; cross-manager calls happen outside lock scope.
3. Dependency graph is frozen before extraction:
   - Allowed: `OAuthManager -> ProviderManager`, `InterfaceManager -> ChainRunner/SessionManager/ProviderManager`.
   - Disallowed: back-references that create cycles.
4. Interim WebSocket bridge is declared:
   - Because WebSocket controller extraction is out of scope, any moved state needed by WS handlers must be exposed via narrow AdminServer forwarding methods (no direct public state access).
5. API parity gate:
   - Define a lightweight endpoint parity checklist (status code + minimal response shape) for critical routes to run after each phase.

### Phase 1: Interface subsystem extraction

1. Create `include/animus_kernel/interfaces/IrcInterface.h`:
   - Move `IrcInterfaceRuntime` class, `Config`, `ChannelConfig`, `ParsedLine` structs.
   - Public interface: `Start()`, `Stop()`, `Send(target, message)`, `IsConnected()`, `GetStatus()`.
   - Message/status callbacks remain (injected by owner).
2. Create `src/kernel/interfaces/IrcInterface.cpp`:
   - Move all IRC runtime implementation (~400 lines from AdminServer.cpp lines 2559–2960ish).
3. Create `include/animus_kernel/admin/InterfaceManager.h` + `src/kernel/admin/InterfaceManager.cpp`:
   - Own `m_interfacesByName`, `m_ircInterfaces`, `m_ircMutex`.
   - `SyncIrcInterfaces()`, `StartIrcInterface()`, `StopIrcInterface()`.
   - Interface CRUD (list, get, patch, enable, disable) with runtime lifecycle hooks.
4. Update `AdminServer` to hold `InterfaceManager*` instead of raw interface state.
5. Wire interface route handlers to call `InterfaceManager` methods.

### Phase 2: Provider + OAuth extraction

1. Create `include/animus_kernel/admin/ProviderManager.h` + `src/kernel/admin/ProviderManager.cpp`:
   - Own `m_providersByName`, `m_providerMutex`, `m_defaultProvider`.
   - Provider CRUD (list, get, create, update, delete, test, capabilities, models).
   - `GetProviderConfig()`, `GetProviderConcurrency()`.
2. Create `include/animus_kernel/admin/OAuthManager.h` + `src/kernel/admin/OAuthManager.cpp`:
   - Own `m_pendingOauthCodeFlowsByProviderId`, `m_oauthFlowMutex`.
   - Device flow: start, poll, exchange, persist.
   - PKCE browser flow: start, exchange, persist.
   - OAuth status endpoint logic.
   - Depends on `ProviderManager` for provider lookup and auth file paths.
3. Update AdminServer route handlers to delegate.

### Phase 3: Agent + Memory + Constitution extraction

1. Create `include/animus_kernel/admin/AgentManager.h` + `src/kernel/admin/AgentManager.cpp`:
   - Own `m_agentCrudMutex`.
   - Agent CRUD (list, get, create, update, delete).
   - Tool config validation.
   - Depends on `AgentStore` (passed in, not owned).
2. Create `include/animus_kernel/admin/MemoryManager.h` + `src/kernel/admin/MemoryManager.cpp`:
   - Own `m_memoryMutex`, `m_memoryLayers`, `m_observationsById`, `m_consolidationState`.
   - Layer CRUD, observation CRUD, consolidation, perspective.
3. Create `include/animus_kernel/admin/ConstitutionManager.h` + `src/kernel/admin/ConstitutionManager.cpp`:
   - Own `m_constitutionMutex`, constitution state.
   - Core/contracts/adaptation CRUD, audit log.
4. Update AdminServer route handlers to delegate.

### Phase 4: AdminServer cleanup

1. Remove all extracted members from `AdminServer.h`.
2. AdminServer retains: lifecycle (`Start`, `Stop`, `RunLoop`), `RegisterHandlersOnce`, manager pointers, config load/save orchestration.
3. Verify all route handlers are thin dispatch (parse → call → format).
4. Add section comments in `RegisterHandlersOnce()` grouping routes by manager.
5. Update `CMakeLists.txt` with all new source files.
6. Full build + test pass.

## Proposed File Layout

```
include/animus_kernel/
  admin/
    ProviderManager.h
    OAuthManager.h
    AgentManager.h
    MemoryManager.h
    ConstitutionManager.h
    InterfaceManager.h
  interfaces/
    IrcInterface.h

src/kernel/
  admin/
    AdminServer.cpp          (lifecycle + route dispatch only, target <1500 lines)
    ProviderManager.cpp
    OAuthManager.cpp
    AgentManager.cpp
    MemoryManager.cpp
    ConstitutionManager.cpp
    InterfaceManager.cpp
  interfaces/
    IrcInterface.cpp
```

## State Ownership Rules

1. Each manager owns its state and the mutex that protects it.
2. AdminServer never accesses manager-owned state directly — always through manager methods.
3. Cross-manager dependencies (e.g., OAuthManager needs ProviderManager for auth file paths) are resolved via pointer injection at construction, not via AdminServer member access.
4. Config load/save orchestration stays in AdminServer (it reads files and calls manager initialization methods).

## Acceptance Criteria

- [x] `IrcInterfaceRuntime` is in its own header/source pair, not inline in AdminServer.cpp.
- [x] `InterfaceManager` owns interface state and IRC runtime lifecycle.
- [x] `ProviderManager` owns provider state, CRUD, and test logic.
- [x] `OAuthManager` owns OAuth flow state and device/browser flow logic.
- [x] `AgentManager` owns agent CRUD state and tool config validation.
- [x] `MemoryManager` owns memory state, layer/observation CRUD, consolidation.
- [x] `ConstitutionManager` owns constitution state and adaptation logic.
- [x] `AdminServer.cpp` no longer owns extracted domain state/mutexes and route handlers are dispatch-only for extracted domains.
- [x] `AdminServer.h` has no raw state maps/mutexes for extracted domains.
- [x] No behavioral change: all existing API endpoints produce identical responses.
- [x] All existing tests pass (`cmake --build && ctest`).
- [x] New source files compile and link (`CMakeLists.txt` updated).
- [x] Each manager class has a clear public interface documented in its header.
- [x] Cross-manager dependencies are explicit (constructor injection), not implicit (shared AdminServer state).

## Testing Plan

1. Add a minimal API parity smoke harness (or scripted checklist) for key endpoints to validate status codes and response shape stability during refactor.
2. Run full test suite before and after each phase. Zero regression is the bar.
3. Manual smoke test after Phase 4: start daemon, hit key endpoints (providers, agents, interfaces, memory, constitution), verify responses unchanged.
4. IRC smoke test after Phase 1: configure IRC interface, verify connect/register/join still works through extracted `IrcInterface`.

## Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| State split breaks concurrent access patterns | Medium | Each manager owns its mutex; no cross-domain locks. Audit lock ordering before merge. |
| Route handler thinness makes error handling inconsistent | Low | Managers return structured results; handlers format uniformly. |
| Circular dependency between managers | Low | Dependency graph is acyclic by design (OAuth → Provider, Interface → ChainRunner). Document in headers. |
| Build breakage from missing includes | Medium | Incremental phases with build verification after each. |
| WebSocket controllers still reference AdminServer members directly | Medium | WebSocket controllers are out of scope for this ticket; if they access moved members, add accessor methods as interim bridge and file a follow-up. |

## Follow-up Tickets (out of scope, noted for roadmap)

1. **WebSocket controller extraction** — `AdminChatWebSocketController` and `AdminObservationWebSocketController` should become standalone classes.
2. **Interface plugin/module loader** — dynamic loading of interface types (IRC, Slack, Telegram) at runtime.
3. **IRC TLS + SASL** — resume Ticket 034 outstanding work against the extracted `IrcInterface`.
4. **AdminServer route handler splitting** — if AdminServer route dispatch is still too large after extraction, split into per-domain handler files included from `RegisterHandlersOnce()`.

## Notes

The 8 MB RSS figure is encouraging — Animus is lean at runtime. This ticket addresses the *structural* problem: a single 8000-line file that every new feature must touch. Decomposition is an investment in velocity, not memory.
