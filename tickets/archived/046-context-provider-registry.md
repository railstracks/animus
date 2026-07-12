# Ticket 046 — Context Provider Registry

**Status:** Shipped (wiring bug fixed 2026-05-15)
**Priority:** P1 (blocks clean 044 implementation)
**Depends on:** 041 (session notes injection in ChainRunner)
**Blocks:** 044 (active memory), 039 (Lua context injection)
**Implemented:** 2026-05-14 (commit b86f7cb)
**Bugfix:** 2026-05-15 (commit 01c8c49) — registry was wired to AdminServer before construction, passing null pointer. Moved wire call to after registry population.

---

## Problem

ChainRunner currently assembles the system prompt through hardcoded injection:

1. `agent->identity` → assigned to `resolvedSystemPrompt`
2. Session notes → appended to `resolvedSystemPrompt`

With active memory (044), project tool context, and eventual Lua plugin context, this ad-hoc pattern doesn't scale. Each new context source requires modifying ChainRunner directly.

## Solution

Introduce a `ContextProviderRegistry` — an ordered list of `IContextProvider` instances that the ChainRunner iterates during prompt assembly.

### Interface

```cpp
// Context provided by a registered provider during prompt assembly.
struct ContextBlock {
    std::string name;       // e.g. "identity", "session_notes", "active_memory"
    std::string content;    // The actual text block
    int priority = 100;     // Lower = earlier in assembly (identity=0, notes=50, active_mem=30)
};

class IContextProvider {
public:
    virtual ~IContextProvider() = default;
    virtual std::string Name() const = 0;
    virtual int Priority() const = 0;  // Ordering hint
    virtual std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) = 0;
};
```

### Registry

```cpp
class ContextProviderRegistry {
public:
    void Register(std::unique_ptr<IContextProvider> provider);
    std::vector<ContextBlock> Assemble(
        const Agent& agent,
        const SessionAccess& session) const;
    // Returns blocks sorted by priority (ascending)
private:
    std::vector<std::unique_ptr<IContextProvider>> m_providers;
};
```

### Built-in Providers

| Provider | Priority | Source | Ticket |
|---|---|---|---|
| `IdentityProvider` | 0 | `agent.identity` | This ticket |
| `SessionNotesProvider` | 50 | `SessionNotesStore` | This ticket |
| `ActiveMemoryProvider` | 30 | Memory layers + observations + ontology | 044 |
| `ProjectTasksProvider` | 40 | Project tool state | Future (040) |
| `LuaContextProvider` | 60 | Lua plugin hooks | 039 |

### ChainRunner Integration

Replace the hardcoded injection in both `ExecuteOnSession` and `ExecuteStreamingOnSession`:

```cpp
// Before (hardcoded):
if (!agent->system_prompt.empty()) resolvedSystemPrompt = agent->system_prompt;
if (m_sessionNotesStore && ...) { resolvedSystemPrompt += notes; }

// After (registry):
auto blocks = m_contextRegistry.Assemble(agent, session);
for (const auto& block : blocks) {
    resolvedSystemPrompt += "\n\n## " + block.name + "\n\n" + block.content;
}
```

### Lua Integration (ticket 039)

Lua plugins register context providers through the Lua host:

```lua
animus.register_context_provider("my_plugin", 70, function(agent, session)
    return "Custom context from my plugin for agent " .. agent.name
end)
```

The Lua runtime wraps these into `IContextProvider` instances.

### Admin API

- `GET /api/v1/context/providers` — list registered providers and their priorities
- `GET /api/v1/context/preview/{agentId}` — preview assembled context for an agent

## Files Changed

- **New:** `include/animus_kernel/IContextProvider.h`
- **New:** `include/animus_kernel/ContextProviderRegistry.h`
- **New:** `src/kernel/context/ContextProviderRegistry.cpp`
- **New:** `src/kernel/context/IdentityProvider.cpp`
- **New:** `src/kernel/context/SessionNotesProvider.cpp`
- **Modified:** `ChainRunner.h` — `ContextProviderRegistry` member replaces direct store pointers
- **Modified:** `ChainRunner.cpp` — use registry for assembly, remove hardcoded injection
- **Modified:** `AgentKernel.cpp` — create registry, register built-in providers
- **Modified:** `AdminServer` — new endpoints for provider listing/preview
- **Modified:** `CMakeLists.txt` — new source files

## Acceptance Criteria

1. `ContextProviderRegistry` implemented with ordered `Provide()` dispatch
2. Identity and session notes extracted into dedicated providers
3. ChainRunner uses registry exclusively — no hardcoded identity/notes injection
4. All existing behavior preserved (no regression in prompt assembly)
5. Admin API endpoint for listing registered providers
6. Syntax-clean on all new/modified files

## Scope Notes

- This ticket does NOT implement active memory (044) or Lua providers (039)
- It creates the infrastructure they slot into
- The registry is C++ only for now; Lua bridge is ticket 039's concern
